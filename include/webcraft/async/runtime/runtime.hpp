#pragma once

#include <coroutine>
#include <concepts>
#include <async/task.h>
#include <exception>
#include <memory>
#include <iostream>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <WinSock2.h>

namespace webcraft::async::runtime
{
    namespace win32
    {
        HANDLE initialize_iocp()
        {
            // Initialize Winsock
            WSADATA wsaData;
            int result = WSAStartup(MAKEWORD(2, 2), &wsaData);

            if (result != 0)
                throw std::runtime_error("Failed to initialize Winsock: " + std::to_string(result));

            // Create an IO Completion Port (IOCP)
            auto iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
            if (iocp == NULL)
                throw std::runtime_error("Failed to create IOCP");

            return iocp;
        }

        void destroy_iocp(HANDLE iocp)
        {
            auto check = CloseHandle(iocp);

            if (!check)
                throw std::runtime_error("Failed to close IOCP handle");

            // Cleanup Winsock
            int result = WSACleanup();
            if (result != 0)
                throw std::runtime_error("Failed to cleanup Winsock: " + std::to_string(result));
        }

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
            if (!result)
                throw std::runtime_error("Failed to post NOP event to IOCP");
        }

        auto wait_and_get_event(HANDLE iocp)
        {
            DWORD bytesTransferred;
            ULONG_PTR completionKey;
            OVERLAPPED *overlapped;

            BOOL result = GetQueuedCompletionStatus(iocp, &bytesTransferred, &completionKey, &overlapped, INFINITE);
            if (!result)
                throw std::runtime_error("Failed to get completion status from IOCP");

            // Process the completion event
            if (overlapped == nullptr)
                throw std::runtime_error("Completion event is null");
            auto event = static_cast<overlapped_event *>(overlapped);
            uint64_t user_data = event->payload;

            delete event; // Clean up the overlapped structure
            return user_data;
        }
    }

    struct callback
    {
        virtual void execute() = 0;
    };

    struct runtime_provider
    {
        HANDLE iocp;

        runtime_provider()
        {
            iocp = win32::initialize_iocp();
        }

        ~runtime_provider()
        {
            win32::destroy_iocp(iocp);
        }
    };

    static auto provider = std::make_shared<runtime_provider>();

    /// @brief yields control to the runtime. will be requeued and executed again.
    /// @return the awaitable for this function
    ::async::task<void> yield()
    {
        struct yield_awaiter
        {
            HANDLE iocp;

            yield_awaiter(HANDLE iocp) : iocp(iocp) {}

            constexpr void await_resume() const {}
            constexpr bool await_ready() const noexcept { return false; }
            void await_suspend(std::coroutine_handle<> handle) noexcept
            {
                win32::post_nop_event(iocp, reinterpret_cast<uint64_t>(handle.address()));
            }
        };

        co_await yield_awaiter(provider->iocp);
    }

    auto wait_and_get_event()
    {
        return win32::wait_and_get_event(provider->iocp);
    }
}