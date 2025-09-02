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

    // Check if thread exists and is joinable before stopping
    if (run_thread && run_thread->joinable())
    {
        // stop running the io_uring loop
        run_thread->request_stop();

        // Wait for thread to finish
        run_thread->join();
    }

    // Reset the thread pointer
    run_thread.reset();
}

#ifdef __linux__
#include <liburing.h>
#include <webcraft/async/runtime/linux.event.hpp>

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
    // while we're running, we will wait for events
    while (!token.stop_requested())
    {
        __kernel_timespec ts = {0, 0};
        ts.tv_nsec = std::chrono::duration_cast<std::chrono::nanoseconds>(wait_timeout).count(); // 10ms timeout

        struct io_uring_cqe *cqe;
        int ret = io_uring_wait_cqe_timeout(&global_ring, &cqe, &ts);
        if (ret < 0 && ret != -ETIME)
        {
            break; // Other error, exit loop
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

    // Only cleanup if we were the ones who initialized it
    io_uring_queue_exit(&global_ring);
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
    return webcraft::async::detail::linux::create_io_uring_event([](struct io_uring_sqe *sqe)
                                                                 { io_uring_prep_nop(sqe); });
}

std::unique_ptr<webcraft::async::detail::runtime_event> webcraft::async::detail::post_sleep_event(std::chrono::steady_clock::duration duration, std::stop_token token)
{
    return webcraft::async::detail::linux::create_io_uring_event([duration](struct io_uring_sqe *sqe)
                                                                 {
                                                                     __kernel_timespec its;
                                                                     its.tv_sec = std::chrono::duration_cast<std::chrono::seconds>(duration).count();
                                                                     its.tv_nsec = std::chrono::duration_cast<std::chrono::nanoseconds>(duration % std::chrono::seconds(1)).count();
                                                                     io_uring_prep_timeout(sqe, &its, 0, 0); }, token);
}

#elif defined(_WIN32)

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <webcraft/async/runtime/windows/windows_timer_manager.hpp>
#include <webcraft/async/runtime/windows.event.hpp>
#include <webcraft/async/runtime/event.hpp>
#include <WinSock2.h>

static HANDLE iocp;
static webcraft::async::runtime::detail::timer_manager timer_manager;
using webcraft::async::detail::windows::overlapped_event;

uint64_t webcraft::async::detail::get_native_handle()
{
    return reinterpret_cast<uint64_t>(iocp);
}

void run_loop(std::stop_token token)
{
    while (!token.stop_requested())
    {
        DWORD bytesTransferred;
        ULONG_PTR completionKey;
        LPOVERLAPPED overlapped;

        BOOL result = GetQueuedCompletionStatus(iocp, &bytesTransferred, &completionKey, &overlapped, static_cast<DWORD>(wait_timeout.count()));

        if (!result)
        {
            DWORD lastError = GetLastError();
            if (lastError == WAIT_TIMEOUT)
            {
                continue; // Timeout, retry
            }

            // If we have a valid overlapped pointer, the I/O operation completed but with an error
            // This is normal for operations like reading past EOF
            if (overlapped != nullptr)
            {
                // Process the completion event even though it failed
                auto *event = reinterpret_cast<overlapped_event *>(overlapped);

                auto runtime_event = event->event;
                if (runtime_event && !event->completed_sync)
                {
                    // For file operations, ERROR_HANDLE_EOF (38) means we've reached end of file
                    // Pass 0 bytes transferred and let the application handle it
                    if (lastError == ERROR_HANDLE_EOF)
                    {
                        runtime_event->try_execute(0); // 0 bytes indicates EOF
                    }
                    else
                    {
                        // Other errors - pass negative error code
                        runtime_event->try_execute(-static_cast<int>(lastError));
                    }
                }
                continue;
            }

            // If no overlapped pointer, this is a serious error
            break; // Other error, exit loop
        }

        if (overlapped == nullptr)
        {
            std::cerr << "Received null event in IOCP loop." << std::endl;
            continue; // Skip if event is null
        }

        auto *event = reinterpret_cast<overlapped_event *>(overlapped);
        auto runtime_event = event->event;
        if (runtime_event && !event->completed_sync)
        {
            // Call the callback function
            runtime_event->try_execute(static_cast<int>(bytesTransferred));
        }
    }

    CloseHandle(iocp);
    WSACleanup();
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
    HANDLE iocp = ::iocp;
    return webcraft::async::detail::windows::create_overlapped_event(
        [iocp](LPDWORD bytesTransferred, LPOVERLAPPED ptr)
        {
            BOOL result = PostQueuedCompletionStatus(iocp, *bytesTransferred, 0, ptr);
            if (!result)
            {
                throw std::runtime_error("Failed to post yield event: " + std::to_string(GetLastError()));
            }
            SetLastError(ERROR_IO_PENDING);
            return false;
        });
}

std::unique_ptr<webcraft::async::detail::runtime_event> webcraft::async::detail::post_sleep_event(std::chrono::steady_clock::duration duration, std::stop_token token)
{
    std::shared_ptr<PTP_TIMER> timer;

    return webcraft::async::detail::create_runtime_event(
        [timer, duration](webcraft::async::detail::runtime_event *ev) mutable
        {
            PTP_TIMER _timer = timer_manager.post_timer_event(
                duration,
                [ev]()
                {
                    ev->try_execute(0);
                });
            timer = std::make_shared<PTP_TIMER>(_timer);
        },
        [timer]
        {
            if (timer && *timer)
            {
                timer_manager.cancel_timer(*timer);
            }
        },
        token);
}

#elif defined(__APPLE__)

#include <sys/event.h>
#include <unistd.h>
#include <webcraft/async/runtime/macos.event.hpp>

static int queue;

uint64_t webcraft::async::detail::get_native_handle()
{
    return static_cast<uint64_t>(queue);
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

int16_t current_filter;
uint32_t current_flags;

int16_t webcraft::async::detail::get_kqueue_filter()
{
    return current_filter;
}
uint32_t webcraft::async::detail::get_kqueue_flags()
{
    return current_flags;
}

void run_loop(std::stop_token token)
{
    // while we're running, we will wait for events
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
        current_filter = event.filter;
        current_flags = event.flags;

        // Call the callback or handle the event based on user_data
        auto *ev = reinterpret_cast<webcraft::async::detail::runtime_callback *>(user_data);
        if (ev)
        {
            // Call the callback function
            ev->try_execute(ret);
        }
    }

    close(queue);
}

std::unique_ptr<webcraft::async::detail::runtime_event> webcraft::async::detail::post_yield_event()
{
    return webcraft::async::detail::macos::create_kqueue_event(
        [](struct kevent *event, void *data)
        {
            // listen to the yield event
            uintptr_t id = (uintptr_t)data;

            EV_SET(event, id, EVFILT_USER, EV_ADD | EV_ENABLE, 0, 0, nullptr);
            int result = kevent(queue, event, 1, nullptr, 0, nullptr);
            if (result != 0)
            {
                throw std::runtime_error("Failed to register yield event: " + std::to_string(result));
            }

            EV_SET(event, id, EVFILT_USER, 0, NOTE_TRIGGER, 0, data);
            result = kevent(queue, event, 1, nullptr, 0, nullptr);
            if (result != 0)
            {
                throw std::runtime_error("Failed to fire yield event: " + std::to_string(result));
            }
        });
}

std::unique_ptr<webcraft::async::detail::runtime_event> webcraft::async::detail::post_sleep_event(std::chrono::steady_clock::duration duration, std::stop_token token)
{
    return webcraft::async::detail::macos::create_kqueue_event(
        [duration](struct kevent *event, void *data)
        {
            // listen to the yield event
            uintptr_t id = (uintptr_t)data;

            EV_SET(event, id, EVFILT_TIMER, EV_ADD | EV_ENABLE, NOTE_NSECONDS, std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count(), data);
            int result = kevent(queue, event, 1, nullptr, 0, nullptr);
            if (result < 0)
            {
                throw std::runtime_error("Failed to spawn timer event to kqueue" + std::to_string(result));
            }
        },
        token);
}

#endif