#include <webcraft/async/runtime/provider.hpp>

#include <utility>

#ifdef _WIN32

#include <webcraft/async/runtime/windows/windows_timer_manager.hpp>
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <WinSock2.h>

#pragma region Windows API

namespace detail::win32
{

    struct overlapped_event : public OVERLAPPED
    {
        uint64_t payload;
    };

    void post_nop_event(HANDLE iocp, uint64_t payload)
    {
        // Post a NOP operation to the IOCP
        overlapped_event *overlapped = new overlapped_event();
        memset(overlapped, 0, sizeof(OVERLAPPED));
        overlapped->payload = payload;

        BOOL result = PostQueuedCompletionStatus(iocp, 0, payload, overlapped);
        // Check if the post operation was successful
        if (!result)
        {
            throw std::runtime_error("Failed to post NOP event to IOCP");
        }
    }

    std::pair<uint64_t, uint64_t> wait_for_event(HANDLE iocp)
    {
        DWORD bytesTransferred;
        ULONG_PTR completionKey;
        OVERLAPPED *overlapped;

        BOOL result = GetQueuedCompletionStatus(iocp, &bytesTransferred, &completionKey, &overlapped, INFINITE);
        if (!result)
        {
            throw std::runtime_error("Failed to get completion status from IOCP");
        }

        // Process the completion event
        if (overlapped == nullptr)
        {
            throw std::runtime_error("Completion event is null");
        }
        auto event = static_cast<overlapped_event *>(overlapped);
        uint64_t user_data = event->payload;

        delete event; // Clean up the overlapped structure
        return std::make_pair(user_data, bytesTransferred);
    }

    PTP_TIMER post_timer_event(HANDLE iocp, webcraft::async::runtime::detail::timer_manager &context, std::chrono::steady_clock::duration duration, uint64_t payload)
    {
        return context.post_timer_event(duration, [iocp, payload]()
                                        {
        // This callback will be called when the timer expires
        post_nop_event(iocp, payload); });
    }

}
#pragma endregion

#pragma region Awaitables and providers

class win32_provider;

class yield_awaiter : public webcraft::async::runtime::detail::runtime_event
{
private:
    win32_provider *provider;

public:
    yield_awaiter(win32_provider *provider) : provider(provider) {}

    // void try_start() noexcept override;
};

// class cancellable_sleep_awaiter : public webcraft::async::runtime::detail::cancellable_runtime_event
// {
// private:
//     win32_provider *provider;
//     PTP_TIMER timer;                                // Timer for the sleep operation
//     std::chrono::steady_clock::duration sleep_time; // Duration to sleep

// public:
//     cancellable_sleep_awaiter(win32_provider *provider,
//                               std::chrono::steady_clock::duration sleep_time,
//                               std::stop_token token)
//         : cancellable_runtime_event(token), provider(provider), sleep_time(sleep_time)
//     {
//     }

//     void try_start() noexcept override;

//     void try_native_cancel() noexcept override;
// };

class win32_provider : public webcraft::async::runtime::detail::runtime_provider
{
private:
    webcraft::async::runtime::detail::timer_manager timer_manager; // Timer manager for handling timers
    HANDLE iocp;                                                   // IOCP handle for the runtime
public:
    win32_provider()
    {
        setup_runtime(); // Setup the runtime environment
    }

    ~win32_provider()
    {
        cleanup_runtime(); // Cleanup the runtime environment
    }

    void setup_runtime()
    {
        // Initialize Winsock
        WSADATA wsaData;
        int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (result != 0)
        {
            throw std::runtime_error("Failed to initialize Winsock");
        }
        // Create an IO Completion Port (IOCP)
        iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);

        if (iocp == NULL)
        {
            WSACleanup(); // Cleanup Winsock if IOCP creation fails
            throw std::runtime_error("Failed to create IOCP");
        }
    }

    void cleanup_runtime()
    {
        auto check = CloseHandle(iocp);
        if (!check)
        {
            throw std::runtime_error("Failed to close IOCP handle");
        }
        // Cleanup Winsock
        int result = WSACleanup();
        if (result != 0)
        {
            throw std::runtime_error("Failed to cleanup Winsock");
        }
    }

    webcraft::async::runtime::detail::runtime_event *wait_and_get_event() override
    {
        auto [payload, ret] = detail::win32::wait_for_event(iocp);
        if (payload == 0)
        {
            return nullptr; // No event was ready
        }

        std::cerr << "Event ready with payload: " << payload << ", bytes transferred: " << ret << std::endl;
        auto event = reinterpret_cast<webcraft::async::runtime::detail::runtime_event *>(payload);
        // event->set_result(static_cast<int>(ret)); // Set the result of the event
        return event; // Return the event that was ready
    }

    ::async::task<void> yield() override
    {
        // co_await yield_awaiter(this); // Use the yield awaiter to yield control back to the runtime
        co_return;
    }

    // ::async::task<bool> sleep_for(std::chrono::steady_clock::duration duration, std::stop_token token) override
    // {
    //     auto sleep_awaiter = std::make_shared<cancellable_sleep_awaiter>(this, duration, token);
    //     co_return co_await *sleep_awaiter; // Use the cancellable sleep awaiter to sleep for the specified duration
    // }

    HANDLE get_iocp_handle() const
    {
        return iocp; // Return the IOCP handle for external use
    }

    webcraft::async::runtime::detail::timer_manager &get_timer_manager()
    {
        return timer_manager; // Return the timer manager for handling timers
    }
};

// void yield_awaiter::try_start() noexcept
// {
//     // Post a NOP event to the IOCP to yield control back to the runtime
//     std::cerr << "Yielding control to the runtime: " << (uint64_t)this << std::endl;
//     detail::win32::post_nop_event(provider->get_iocp_handle(), reinterpret_cast<uint64_t>(this));
// }

// void cancellable_sleep_awaiter::try_start() noexcept
// {
//     // Create a timer for the sleep operation
//     timer = detail::win32::post_timer_event(provider->get_iocp_handle(), provider->get_timer_manager(), sleep_time, reinterpret_cast<uint64_t>(this));
// }

// void cancellable_sleep_awaiter::try_native_cancel() noexcept
// {
//     // Cancel the timer if it is still pending
//     provider->get_timer_manager().cancel_timer(timer);
// }

static auto provider_instance = std::make_shared<win32_provider>();

#pragma endregion

#elif defined(__linux__)

#include <liburing.h>

class linux_provider : public webcraft::async::runtime::detail::runtime_provider
{
public:
    void setup_runtime() override
    {
        // TODO: Setup the runtime environment, such as initializing io_uring or other resources
    }

    void cleanup_runtime() override
    {
        // TODO: Cleanup the runtime environment, such as closing io_uring or other resources
    }

    webcraft::async::runtime::detail::runtime_event *wait_and_get_event() override
    {
        // TODO: Implement the logic to wait for an event and return it
        return nullptr;
    }

    ::async::task<void> yield() override
    {
        // TODO: Implement the logic to yield control back to the runtime
        co_return;
    }

    ::async::task<void> sleep_for(std::chrono::steady_clock::duration duration, std::stop_token token) override
    {
        // TODO: Implement the logic to sleep for the specified duration
        co_return;
    }
};

static auto provider_instance = std::make_shared<linux_provider>();

#elif defined(__APPLE__)

#include <sys/event.h>

class macos_provider : public webcraft::async::runtime::detail::runtime_provider
{
public:
    void setup_runtime() override
    {
        // TODO: Setup the runtime environment, such as initializing kqueue or other resources
    }

    void cleanup_runtime() override
    {
        // TODO: Cleanup the runtime environment, such as closing kqueue or other resources
    }

    webcraft::async::runtime::detail::runtime_event *wait_and_get_event() override
    {
        // TODO: Implement the logic to wait for an event and return it
        return nullptr;
    }

    ::async::task<void> yield() override
    {
        // TODO: Implement the logic to yield control back to the runtime
        co_return;
    }

    ::async::task<void> sleep_for(std::chrono::steady_clock::duration duration, std::stop_token token) override
    {
        // TODO: Implement the logic to sleep for the specified duration
        co_return;
    }
};

static auto provider_instance = std::make_shared<macos_provider>();

#else

#error "Unsupported platform for runtime provider"
#endif

std::shared_ptr<webcraft::async::runtime::detail::runtime_provider> webcraft::async::runtime::detail::get_runtime_provider()
{
    return provider_instance; // Return the platform-specific provider instance
}