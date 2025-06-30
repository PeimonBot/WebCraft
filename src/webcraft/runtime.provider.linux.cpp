#ifdef __linux__

#include <webcraft/async/runtime/provider.hpp>
#include <liburing.h>
#include <mutex>

using namespace webcraft::async::runtime::detail;

static std::mutex provider_mutex;

class yield_event : public native_runtime_event
{
private:
    struct io_uring_sqe *sqe;
    struct io_uring *ring;

public:
    yield_event(struct io_uring *ring, std::function<void()> callback) : native_runtime_event(std::move(callback))
    {
        this->ring = ring;
        sqe = io_uring_get_sqe(ring);
        if (!sqe)
        {
            throw std::runtime_error("Failed to get SQE from io_uring");
        }

        io_uring_prep_nop(sqe);
        io_uring_sqe_set_data64(sqe, reinterpret_cast<uint64_t>(this));
    }

    ~yield_event() = default;

    void start_async() override
    {
        std::lock_guard<std::mutex> lock(provider_mutex);
        int ret = io_uring_submit(ring);
        if (ret < 0)
        {
            throw std::runtime_error("Failed to submit SQE to io_uring: " + std::to_string(ret));
        }
    }
};

class timer_event : public native_runtime_event
{
private:
    struct io_uring_sqe *sqe;
    struct io_uring *ring;
    struct __kernel_timespec its;

public:
    timer_event(struct io_uring *ring, std::function<void()> callback, std::chrono::steady_clock::duration duration) : native_runtime_event(std::move(callback))
    {
        this->ring = ring;
        sqe = io_uring_get_sqe(ring);
        if (!sqe)
        {
            throw std::runtime_error("Failed to get SQE from io_uring");
        }

        its.tv_sec = std::chrono::duration_cast<std::chrono::seconds>(duration).count();
        its.tv_nsec = std::chrono::duration_cast<std::chrono::nanoseconds>(duration % std::chrono::seconds(1)).count();

        io_uring_prep_timeout(sqe, &its, 0, 0);
        io_uring_sqe_set_data64(sqe, reinterpret_cast<uint64_t>(this));
    }

    ~timer_event() = default;

    void start_async() override
    {
        std::lock_guard<std::mutex> lock(provider_mutex);
        int ret = io_uring_submit(ring);
        if (ret < 0)
        {
            throw std::runtime_error("Failed to submit SQE to io_uring: " + std::to_string(ret));
        }
    }

    void cancel() override
    {
        std::lock_guard<std::mutex> lock(provider_mutex);
        struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
        if (!sqe)
        {
            throw std::runtime_error("Failed to get SQE from io_uring");
        }

        io_uring_prep_cancel(sqe, this, 0);
        io_uring_sqe_set_data(sqe, this);
        int ret = io_uring_submit(ring);
        if (ret < 0)
        {
            throw std::runtime_error("Failed to submit SQE for cancel event: " + std::to_string(ret));
        }

        native_runtime_event::cancel(); // Mark the event as canceled
    }
};

class io_uring_runtime_provider : public runtime_provider
{
private:
    struct io_uring ring;
    std::atomic<bool> setup;
    std::atomic<bool> cleanup;

public:
    io_uring_runtime_provider()
    {
        setup_runtime();
    }

    ~io_uring_runtime_provider()
    {
        cleanup_runtime();
    }

    void setup_runtime() override
    {
        bool expected = false;
        if (setup.compare_exchange_strong(expected, true))
        {
            int ret = io_uring_queue_init(256, &ring, 0);
            if (ret < 0)
            {
                throw std::runtime_error("Failed to initialize io_uring: " + std::to_string(ret));
            }
        }
    }

    void cleanup_runtime() override
    {
        bool expected = false;
        if (cleanup.compare_exchange_strong(expected, true))
        {
            io_uring_queue_exit(&ring);
        }
    }

    std::unique_ptr<native_runtime_event> create_nop_event(std::function<void()> callback) override
    {
        return std::make_unique<yield_event>(&ring, std::move(callback));
    }

    std::unique_ptr<native_runtime_event> create_timer_event(std::chrono::steady_clock::duration duration, std::function<void()> callback) override
    {
        return std::make_unique<timer_event>(&ring, std::move(callback), duration);
    }

    native_runtime_event *wait_and_get_event() override
    {
        struct io_uring_cqe *cqe;
        int ret = io_uring_wait_cqe(&ring, &cqe);
        if (ret < 0)
        {
            throw std::runtime_error("Failed to wait for completion event: " + std::to_string(ret));
        }

        if (cqe)
        {
            auto user_data = reinterpret_cast<native_runtime_event *>(cqe->user_data);
            user_data->set_result(cqe->res);
            io_uring_cqe_seen(&ring, cqe);
            return user_data;
        }
        return nullptr;
    }
};

static auto provider = std::make_shared<io_uring_runtime_provider>();

std::shared_ptr<webcraft::async::runtime::detail::runtime_provider> webcraft::async::runtime::detail::get_runtime_provider()
{
    return provider;
}

#endif