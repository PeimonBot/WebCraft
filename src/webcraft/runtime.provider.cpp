#include <webcraft/async/runtime.hpp>
#include <mutex>
#include <thread>
#include <liburing.h>
#include <iostream>
#include <string>
#include <cstring>

static std::mutex io_uring_mutex;
static io_uring global_ring;
static std::unique_ptr<std::jthread> io_uring_thread;
static std::atomic<bool> is_running{false};

using namespace std::chrono_literals;

struct runtime_event
{
    std::function<void()> callback;
    int *result;
};

void run_io_uring_loop(std::stop_token token)
{
    // perform the following when we're done with the io_uring loop
    std::stop_callback callback(token, []()
                                {
        std::lock_guard<std::mutex> lock(io_uring_mutex);
        io_uring_queue_exit(&global_ring); });

    // while we're running, we will wait for events
    std::cout << "IO_uring loop started." << std::endl;
    while (!token.stop_requested())
    {
        __kernel_timespec ts = {0, std::chrono::duration_cast<std::chrono::nanoseconds>(100ms).count()}; // No timeout

        struct io_uring_cqe *cqe;
        int ret = io_uring_wait_cqe_timeout(&global_ring, &cqe, &ts);
        if (ret < 0)
        {
            if (ret == -EINTR)
                continue; // Interrupted, retry
            break;        // Other error, exit loop
        }

        if (cqe)
        {
            // Process the completion event
            auto user_data = cqe->user_data;
            io_uring_cqe_seen(&global_ring, cqe);
            // Call the callback or handle the event based on user_data
            auto *event = reinterpret_cast<runtime_event *>(user_data);
            if (event)
            {
                // Call the callback function
                event->callback();
                if (event->result)
                {
                    *event->result = cqe->res; // Store the result if provided
                }
                delete event;
            }
        }
    }
}

std::stop_token webcraft::async::get_stop_token()
{
    return io_uring_thread->get_stop_token();
}

void webcraft::async::detail::initialize_runtime() noexcept
{
    if (is_running.exchange(true))
    {
        return; // Runtime already initialized
    }

    std::lock_guard<std::mutex> lock(io_uring_mutex);
    auto ret = io_uring_queue_init(1024, &global_ring, 0);

    if (ret < 0)
    {
        // Marked noexcept so can't throw exceptions, handle error gracefully
        std::cerr << "Failed to initialize io_uring: " << std::strerror(-ret) << std::endl;
        return;
    }
}

void webcraft::async::detail::shutdown_runtime() noexcept
{
    if (!is_running.exchange(false))
    {
        return; // Runtime not running, nothing to shut down
    }

    std::lock_guard<std::mutex> lock(io_uring_mutex);

    // stop running the io_uring loop
    io_uring_thread->request_stop();

    // join the thread if still possible
    if (io_uring_thread && io_uring_thread->joinable())
    {
        io_uring_thread->join();
    }

    io_uring_thread.reset();
}

void webcraft::async::detail::post_yield_event(std::function<void()> func, int *result)
{
    // Directly call the function to simulate posting a yield event
    std::lock_guard<std::mutex> lock(io_uring_mutex);
    runtime_event *event = new runtime_event{func, result};

    struct io_uring_sqe *sqe = io_uring_get_sqe(&global_ring);
    if (!sqe)
    {
        delete event; // Clean up if we can't get an SQE
        throw std::runtime_error("Failed to get SQE from io_uring");
    }

    // Prepare a NOP operation to signal the completion of the task
    io_uring_prep_nop(sqe);
    io_uring_sqe_set_data64(sqe, reinterpret_cast<uint64_t>(event));
    int ret = io_uring_submit(&global_ring);
    if (ret < 0)
    {
        delete event; // Clean up if submission fails
        throw std::runtime_error("Failed to submit SQE to io_uring: " + std::string(std::strerror(-ret)));
    }
}

void webcraft::async::detail::post_sleep_event(std::function<void()> func, std::chrono::steady_clock::duration duration, std::stop_token token, int *result)
{
    std::lock_guard<std::mutex> lock(io_uring_mutex);
    runtime_event *event = new runtime_event{func, result};

    struct io_uring_sqe *sqe = io_uring_get_sqe(&global_ring);
    if (!sqe)
    {
        delete event; // Clean up if we can't get an SQE
        throw std::runtime_error("Failed to get SQE from io_uring");
    }

    __kernel_timespec its;
    its.tv_sec = std::chrono::duration_cast<std::chrono::seconds>(duration).count();
    its.tv_nsec = std::chrono::duration_cast<std::chrono::nanoseconds>(duration % std::chrono::seconds(1)).count();
    io_uring_prep_timeout(sqe, &its, 0, 0);
    io_uring_sqe_set_data64(sqe, reinterpret_cast<uint64_t>(event));
    int ret = io_uring_submit(&global_ring);
    if (ret < 0)
    {
        delete event; // Clean up if submission fails
        throw std::runtime_error("Failed to submit SQE to io_uring: " + std::string(std::strerror(-ret)));
    }
}
