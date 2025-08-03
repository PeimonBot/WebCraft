#include <webcraft/async/runtime.hpp>
#include <mutex>
#include <thread>
#include <iostream>
#include <string>
#include <cstring>
#include <chrono>

using namespace std::chrono_literals;
static std::unique_ptr<std::jthread> run_thread;
static std::atomic<bool> is_running{false};
constexpr auto wait_timeout = 10ms;

std::stop_token webcraft::async::get_stop_token()
{
    return run_thread->get_stop_token();
}

void run_loop(std::stop_token token);

bool start_runtime_async() noexcept;

void webcraft::async::detail::initialize_runtime() noexcept
{
    if (is_running.exchange(true))
    {
        return; // Runtime already initialized
    }

    if (!start_runtime_async())
    {
        return;
    }

    run_thread = std::make_unique<std::jthread>(run_loop);
}

void webcraft::async::detail::shutdown_runtime() noexcept
{
    if (!is_running.exchange(false))
    {
        return; // Runtime not running, nothing to shut down
    }

    // stop running the io_uring loop
    run_thread->request_stop();

    // join the thread if still possible
    if (run_thread && run_thread->joinable())
    {
        run_thread->join();
    }

    run_thread.reset();
}

#ifdef __linux__
#include <liburing.h>

static std::mutex io_uring_mutex;
static io_uring global_ring;

std::mutex &webcraft::async::detail::get_runtime_mutex()
{
    return io_uring_mutex;
}

uint64_t webcraft::async::detail::get_native_handle()
{
    return reinterpret_cast<uint64_t>(&global_ring);
}

void run_loop(std::stop_token token)
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
        __kernel_timespec ts = {0, 0};
        ts.tv_nsec = std::chrono::duration_cast<std::chrono::nanoseconds>(wait_timeout).count(); // 10ms timeout

        struct io_uring_cqe *cqe;
        int ret = io_uring_wait_cqe_timeout(&global_ring, &cqe, &ts);
        if (ret < 0 && ret != -ETIME)
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
            auto *event = reinterpret_cast<webcraft::async::detail::runtime_event *>(user_data);
            if (event)
            {
                // Call the callback function
                event->try_execute(cqe->res);
            }
        }
    }
}

bool start_runtime_async() noexcept
{
    auto ret = io_uring_queue_init(1024, &global_ring, 0);

    if (ret < 0)
    {
        // Marked noexcept so can't throw exceptions, handle error gracefully
        std::cerr << "Failed to initialize io_uring: " << std::strerror(-ret) << std::endl;
        return false;
    }
    return true;
}

std::unique_ptr<webcraft::async::detail::runtime_event> webcraft::async::detail::post_yield_event()
{
    // Directly call the function to simulate posting a yield event
    struct yield_event final : webcraft::async::detail::runtime_event
    {

    public:
        yield_event(std::stop_token token = webcraft::async::get_stop_token()) : runtime_event(token) {}

        void try_native_cancel() override
        {
        }

        void try_start() override
        {
            std::lock_guard<std::mutex> lock(io_uring_mutex);
            struct io_uring_sqe *sqe = io_uring_get_sqe(&global_ring);
            if (!sqe)
            {
                throw std::runtime_error("Failed to get SQE from io_uring");
            }

            // Prepare a NOP operation to signal the completion of the task
            io_uring_prep_nop(sqe);
            io_uring_sqe_set_data64(sqe, reinterpret_cast<uint64_t>(this));
            int ret = io_uring_submit(&global_ring);
            if (ret < 0)
            {
                throw std::runtime_error("Failed to submit SQE to io_uring: " + std::string(std::strerror(-ret)));
            }
        }
    };

    return std::make_unique<yield_event>();
}

std::unique_ptr<webcraft::async::detail::runtime_event> webcraft::async::detail::post_sleep_event(std::chrono::steady_clock::duration duration, std::stop_token token)
{

    struct sleep_event final : webcraft::async::detail::runtime_event
    {
    private:
        std::chrono::steady_clock::duration duration;

    public:
        sleep_event(std::chrono::steady_clock::duration duration, std::stop_token token) : runtime_event(token), duration(duration) {}

        void try_native_cancel() override
        {
            std::lock_guard<std::mutex> lock(io_uring_mutex);
            struct io_uring_sqe *sqe = io_uring_get_sqe(&global_ring);
            if (!sqe)
            {
                throw std::runtime_error("Failed to get SQE from io_uring");
            }
            io_uring_prep_cancel64(sqe, reinterpret_cast<uint64_t>(this), IORING_ASYNC_CANCEL_USERDATA);
            int ret = io_uring_submit(&global_ring);
            if (ret < 0)
            {
                throw std::runtime_error("Failed to submit SQE to io_uring: " + std::string(std::strerror(-ret)));
            }
        }

        void try_start() override
        {
            std::lock_guard<std::mutex> lock(io_uring_mutex);
            struct io_uring_sqe *sqe = io_uring_get_sqe(&global_ring);
            if (!sqe)
            {
                throw std::runtime_error("Failed to get SQE from io_uring");
            }

            __kernel_timespec its;
            its.tv_sec = std::chrono::duration_cast<std::chrono::seconds>(duration).count();
            its.tv_nsec = std::chrono::duration_cast<std::chrono::nanoseconds>(duration % std::chrono::seconds(1)).count();
            io_uring_prep_timeout(sqe, &its, 0, 0);
            io_uring_sqe_set_data64(sqe, reinterpret_cast<uint64_t>(this));
            int ret = io_uring_submit(&global_ring);
            if (ret < 0)
            {
                throw std::runtime_error("Failed to submit SQE to io_uring: " + std::string(std::strerror(-ret)));
            }
        }
    };

    return std::make_unique<sleep_event>(duration, token);
}

#elif defined(_WIN32)

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <webcraft/async/runtime/windows/windows_timer_manager.hpp>
#include <WinSock2.h>

static HANDLE iocp;
static webcraft::async::runtime::detail::timer_manager timer_manager;

uint64_t webcraft::async::detail::get_native_handle()
{
    return reinterpret_cast<uint64_t>(iocp);
}

struct overlapped_event : public OVERLAPPED
{
    webcraft::async::detail::runtime_event *event;

    overlapped_event(webcraft::async::detail::runtime_event *ev) : event(ev)
    {
        ZeroMemory(this, sizeof(OVERLAPPED));
    }
};

void run_loop(std::stop_token token)
{
    std::stop_callback callback(token, []()
                                {
        WSACleanup();
        if (iocp)
        {
            CloseHandle(iocp);
            iocp = nullptr;
        } });
    std::cout << "IOCP loop started." << std::endl;

    while (!token.stop_requested())
    {
        DWORD bytesTransferred;
        ULONG_PTR completionKey;
        LPOVERLAPPED overlapped;

        BOOL result = GetQueuedCompletionStatus(iocp, &bytesTransferred, &completionKey, &overlapped, static_cast<DWORD>(wait_timeout.count()));
        if (!result)
        {
            if (GetLastError() == WAIT_TIMEOUT)
            {
                continue; // Timeout, retry
            }

            std::cerr << "GetQueuedCompletionStatus failed: " << GetLastError() << std::endl;
            break; // Other error, exit loop
        }

        // Process the completion event
        auto *event = reinterpret_cast<overlapped_event *>(overlapped);
        if (event == nullptr)
        {
            std::cerr << "Received null event in IOCP loop." << std::endl;
            continue; // Skip if event is null
        }
        auto runtime_event = event->event;
        if (runtime_event)
        {
            // Call the callback function
            runtime_event->try_execute(static_cast<int>(bytesTransferred));
        }
        delete event; // Clean up the overlapped structure
    }
}

bool start_runtime_async() noexcept
{
    // Windows does not require special initialization for async operations
    iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    if (iocp == nullptr)
    {
        std::cerr << "Failed to create IOCP: " << GetLastError() << std::endl;
        return false;
    }

    WSAData wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        std::cerr << "WSAStartup failed: " << WSAGetLastError() << std::endl;
        CloseHandle(iocp);
        return false;
    }

    return true;
}

std::unique_ptr<webcraft::async::detail::runtime_event> webcraft::async::detail::post_yield_event()
{
    struct yield_event final : webcraft::async::detail::runtime_event
    {
    private:
        overlapped_event *overlapped;

    public:
        yield_event(std::stop_token token = webcraft::async::get_stop_token()) : runtime_event(token)
        {
            overlapped = new overlapped_event(this);
        }

        ~yield_event() = default;

        void try_native_cancel() override
        {
        }

        void try_start() override
        {
            BOOL result = PostQueuedCompletionStatus(iocp, 0, 0, overlapped);
            if (!result)
            {
                throw std::runtime_error("Failed to post yield event: " + std::to_string(GetLastError()));
            }
        }
    };

    return std::make_unique<yield_event>(); // Windows does not support yield events in the same way
}

std::unique_ptr<webcraft::async::detail::runtime_event> webcraft::async::detail::post_sleep_event(std::chrono::steady_clock::duration duration, std::stop_token token)
{
    struct sleep_event final : webcraft::async::detail::runtime_event
    {
    private:
        std::chrono::steady_clock::duration duration;
        PTP_TIMER timer;

    public:
        sleep_event(std::chrono::steady_clock::duration duration, std::stop_token token) : runtime_event(token), duration(duration)
        {
        }

        void try_native_cancel() override
        {
            timer_manager.cancel_timer(timer);
        }

        void try_start() override
        {
            runtime_event *ev = this;
            timer = timer_manager.post_timer_event(duration, [ev]()
                                                   {
                                                       ev->try_execute(0); // Indicate completion with 0 result
                                                   });
        }
    };

    return std::make_unique<sleep_event>(duration, token); // Windows does not support sleep events in the same way
}

#elif defined(__APPLE__)

#include <sys/event.h>
#include <unistd.h>

static int queue;

uint64_t webcraft::async::detail::get_native_handle()
{
    return reinterpret_cast<uint64_t>(queue);
}

bool start_runtime_async() noexcept
{
    queue = kqueue();

    if (queue == -1)
    {
        // Marked noexcept so can't throw exceptions, handle error gracefully
        std::cerr << "Failed to initialize kqueue: " << std::strerror(queue) << std::endl;
        return false;
    }

    return true;
}

void run_loop(std::stop_token token)
{
    // perform the following when we're done with the io_uring loop
    std::stop_callback callback(token, []
                                { close(queue); });

    // while we're running, we will wait for events
    std::cout << "kqueue loop started." << std::endl;
    while (!token.stop_requested())
    {
        timespec ts = {0, 0};
        ts.tv_nsec = std::chrono::duration_cast<std::chrono::nanoseconds>(wait_timeout).count(); // 10ms timeout

        struct kevent event;
        int ret = kevent(queue, nullptr, 0, &event, 1, &ts);

        if (ret == 0)
            continue; // Timeout has occured
        if (ret == -1)
        {
            break; // Error has occured
        }

        // Process the completion event
        auto user_data = event.udata;

        // remove yield event listener
        EV_SET(&event, event.ident, event.filter, EV_DELETE, 0, 0, nullptr);
        ret = kevent(queue, &event, 1, nullptr, 0, nullptr);
        if (ret < 0)
        {
            std::cerr << "An error has occured with deleting the event" << ret << std::endl;
            break;
        }
        // Call the callback or handle the event based on user_data
        auto *ev = reinterpret_cast<webcraft::async::detail::runtime_event *>(user_data);
        if (ev)
        {
            // Call the callback function
            ev->try_execute(ret);
        }
    }
}

std::unique_ptr<webcraft::async::detail::runtime_event> webcraft::async::detail::post_yield_event()
{
    // Directly call the function to simulate posting a yield event
    struct yield_event final : webcraft::async::detail::runtime_event
    {
    private:
        uintptr_t id;

    public:
        yield_event(std::stop_token token = webcraft::async::get_stop_token()) : runtime_event(token)
        {
            id = reinterpret_cast<uintptr_t>(this);
        }

        void try_native_cancel() override
        {
        }

        void try_start() override
        {
            // listen to the yield event
            struct kevent event;
            EV_SET(&event, id, EVFILT_USER, EV_ADD | EV_ENABLE, 0, 0, nullptr);
            int result = kevent(queue, &event, 1, nullptr, 0, nullptr);
            if (result != 0)
            {
                throw std::runtime_error("Failed to register yield event: " + std::to_string(result));
            }

            EV_SET(&event, id, EVFILT_USER, 0, NOTE_TRIGGER, 0, this);
            result = kevent(queue, &event, 1, nullptr, 0, nullptr);
            if (result != 0)
            {
                throw std::runtime_error("Failed to fire yield event: " + std::to_string(result));
            }
        }
    };

    return std::make_unique<yield_event>();
}

std::unique_ptr<webcraft::async::detail::runtime_event> webcraft::async::detail::post_sleep_event(std::chrono::steady_clock::duration duration, std::stop_token token)
{

    struct sleep_event final : webcraft::async::detail::runtime_event
    {
    private:
        std::chrono::steady_clock::duration duration;
        uintptr_t id;

    public:
        sleep_event(std::chrono::steady_clock::duration duration, std::stop_token token) : runtime_event(token), duration(duration)
        {
            id = reinterpret_cast<uintptr_t>(this);
        }

        void try_native_cancel() override
        {
            struct kevent event;
            // remove yield event listener
            EV_SET(&event, id, EVFILT_TIMER, EV_DELETE, 0, 0, nullptr);
            int result = kevent(queue, &event, 1, nullptr, 0, nullptr);
            if (result < 0)
            {
                throw std::runtime_error("Failed to remove event from kqueue: " + std::to_string(result));
            }
        }

        void try_start() override
        {
            struct kevent event;
            EV_SET(&event, id, EVFILT_TIMER, EV_ADD | EV_ENABLE, NOTE_NSECONDS, std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count(), this);
            int result = kevent(queue, &event, 1, nullptr, 0, nullptr);
            if (result < 0)
            {
                throw std::runtime_error("Failed to spawn timer event to kqueue" + std::to_string(result));
            }
        }
    };

    return std::make_unique<sleep_event>(duration, token);
}

#endif