#pragma once

#include <coroutine>
#include <concepts>
#include <exception>
#include <memory>
#include <iostream>
#include <stdexec/execution.hpp>
#include <stop_token>

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

    // Generic runtime event
    class runtime_event
    {
    private:
        int result;

    public:
        virtual void execute_callback() = 0;

        int get_result() const
        {
            return result;
        }

        void set_result(int res)
        {
            result = res;
        }
    };

    class runtime_scheduler
    {
    private:
        std::stop_source stop_source;

    public:
        runtime_scheduler() {}
        ~runtime_scheduler()
        {
            finish();
        }

        void run_until_stopped()
        {
            while (!stop_source.stop_requested())
            {
                try
                {
                    // Wait for an event to be posted
                    auto ptr = win32::wait_and_get_event(win32::initialize_iocp());
                    // Process the event
                    auto *event = reinterpret_cast<runtime_event *>(ptr);
                    if (event)
                    {
                        event->execute_callback();
                        delete event; // Clean up the event after processing
                    }
                    else
                    {
                        std::cerr << "Received null event pointer." << std::endl;
                    }
                }
                catch (const std::exception &e)
                {
                    std::cerr << "Error: " << e.what() << std::endl;
                }
            }
        }

        void start(stdexec::sender auto &&s)
        {
            // Connect it some how, post it idk
        }

        void finish()
        {
            stop_source.request_stop();
        }
    };
}