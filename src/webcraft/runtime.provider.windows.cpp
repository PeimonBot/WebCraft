#ifdef _WIN32

#include <webcraft/async/runtime/provider.hpp>
#include <mutex>
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <WinSock2.h>
#include <webcraft/async/runtime/windows/windows_timer_manager.hpp>

using namespace webcraft::async::runtime::detail;

struct overlapped_event : public OVERLAPPED
{
    native_runtime_event *event;
};

class yield_event : public native_runtime_event
{
private:
    overlapped_event overlapped;
    HANDLE iocp;

public:
    yield_event(HANDLE iocp, std::function<void()> callback) : native_runtime_event(std::move(callback)), iocp(iocp)
    {
        memset(&overlapped, 0, sizeof(overlapped_event));
        overlapped.event = this; // Set the event pointer to this instance
    }

    ~yield_event() = default;

    void start_async() override
    {
        // Post the overlapped event to the IOCP
        if (!PostQueuedCompletionStatus(iocp, 0, 0, &overlapped))
        {
            throw std::runtime_error("Failed to post NOP event to IOCP: " + std::to_string(GetLastError()));
        }
    }

    void cancel() override
    {
    }
};

class timer_event : public native_runtime_event
{
private:
    overlapped_event overlapped;
    HANDLE iocp;
    timer_manager &manager;
    std::chrono::steady_clock::duration duration;
    PTP_TIMER timer = nullptr;

public:
    timer_event(HANDLE iocp, timer_manager &manager, std::function<void()> callback, std::chrono::steady_clock::duration duration) : native_runtime_event(std::move(callback)), iocp(iocp), manager(manager), duration(duration)
    {
        memset(&overlapped, 0, sizeof(overlapped_event));
        overlapped.event = this;
    }

    ~timer_event() = default;

    void start_async() override
    {
        timer = manager.post_timer_event(duration, [this]()
                                         {
            if (!PostQueuedCompletionStatus(iocp, 0, 0, &overlapped))
            {
                throw std::runtime_error("Failed to post timer event to IOCP: " + std::to_string(GetLastError()));
            } });
    }

    void cancel() override
    {
        manager.cancel_timer(timer);

        native_runtime_event::cancel();

        memset(&overlapped, 0, sizeof(overlapped_event));
        overlapped.event = this;

        if (!PostQueuedCompletionStatus(iocp, 0, 0, &overlapped))
        {
            throw std::runtime_error("Failed to post cancel event to IOCP: " + std::to_string(GetLastError()));
        }
    }
};

class iocp_runtime_provider : public runtime_provider
{
private:
    std::atomic<bool> setup;
    std::atomic<bool> cleanup;
    HANDLE iocp;
    timer_manager manager;

public:
    iocp_runtime_provider()
    {
        setup_runtime();
    }

    ~iocp_runtime_provider()
    {
        cleanup_runtime();
    }

    void setup_runtime() override
    {
        bool expected = false;
        if (setup.compare_exchange_strong(expected, true))
        {
            // Initialize Winsock
            WSADATA wsaData;
            int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
            if (result != 0)
            {
                throw std::runtime_error("Failed to initialize Winsock: " + std::to_string(result));
            }

            // Create an IO Completion Port (IOCP)
            iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
            if (iocp == NULL)
            {
                throw std::runtime_error("Failed to create IOCP: " + std::to_string(GetLastError()));
            }
        }
    }

    void cleanup_runtime() override
    {
        bool expected = false;
        if (cleanup.compare_exchange_strong(expected, true))
        {
            auto check = CloseHandle(iocp);
            if (!check)
            {
                throw std::runtime_error("Failed to close IOCP handle: " + std::to_string(GetLastError()));
            }

            // Cleanup Winsock
            int result = WSACleanup();
            if (result != 0)
            {
                throw std::runtime_error("Failed to cleanup Winsock: " + std::to_string(result));
            }
        }
    }

    std::unique_ptr<native_runtime_event> create_nop_event(std::function<void()> callback) override
    {
        return std::make_unique<yield_event>(iocp, std::move(callback));
    }

    std::unique_ptr<native_runtime_event> create_timer_event(std::chrono::steady_clock::duration duration, std::function<void()> callback) override
    {
        return std::make_unique<timer_event>(iocp, manager, std::move(callback), duration);
    }

    native_runtime_event *wait_and_get_event() override
    {
        DWORD bytesTransferred;
        ULONG_PTR completionKey;
        OVERLAPPED *overlapped;

        BOOL result = GetQueuedCompletionStatus(iocp, &bytesTransferred, &completionKey, &overlapped, INFINITE);

        if (!result && !overlapped)
        {
            throw std::runtime_error("Failed to get completion status from IOCP: " + std::to_string(GetLastError()));
        }

        overlapped_event *overlappedEvent = static_cast<overlapped_event *>(overlapped);

        auto event = overlappedEvent->event;
        event->set_result(static_cast<int>(bytesTransferred)); // Set the result to the number of bytes transferred
        return event;                                          // Return the event that was completed
    }
};

static auto provider = std::make_shared<iocp_runtime_provider>();

std::shared_ptr<webcraft::async::runtime::detail::runtime_provider> webcraft::async::runtime::detail::get_runtime_provider()
{
    return provider;
}

#endif