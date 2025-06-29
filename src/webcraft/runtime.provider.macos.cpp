#ifdef __APPLE__

#include <webcraft/async/runtime/provider.hpp>
#include <liburing.h>
#include <mutex>

using namespace webcraft::async::runtime::detail;

static std::mutex provider_mutex;

class yield_event : public native_runtime_event
{
public:
    yield_event(std::function<void()> callback) : native_runtime_event(std::move(callback))
    {
    }

    ~yield_event() = default;

    void start_async() override
    {
    }

    void cancel() override
    {
    }
};

class timer_event : public native_runtime_event
{
public:
    timer_event(std::function<void()> callback, std::chrono::steady_clock::duration duration) : native_runtime_event(std::move(callback))
    {
    }

    ~timer_event() = default;

    void start_async() override
    {
    }

    void cancel() override
    {
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
        }
    }

    void cleanup_runtime() override
    {
        bool expected = false;
        if (cleanup.compare_exchange_strong(expected, true))
        {
        }
    }

    std::unique_ptr<native_runtime_event> create_nop_event(std::function<void()> callback) override
    {
        return std::make_unique<yield_event>(std::move(callback));
    }

    std::unique_ptr<native_runtime_event> create_timer_event(std::chrono::steady_clock::duration duration, std::function<void()> callback) override
    {
        return std::make_unique<timer_event>(std::move(callback), duration);
    }

    native_runtime_event *wait_and_get_event() override
    {
        return nullptr;
    }
};

static auto provider = std::make_shared<io_uring_runtime_provider>();

std::shared_ptr<webcraft::async::runtime::detail::runtime_provider> webcraft::async::runtime::detail::get_runtime_provider()
{
    return provider;
}

#endif