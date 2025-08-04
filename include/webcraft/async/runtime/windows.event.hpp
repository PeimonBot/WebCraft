#pragma once

#ifdef _WIN32
#include <webcraft/async/runtime.hpp>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace webcraft::async::detail::windows
{
    struct overlapped_event : public OVERLAPPED
    {
        webcraft::async::detail::runtime_event *event;

        overlapped_event(webcraft::async::detail::runtime_event *ev) : event(ev)
        {
            ZeroMemory(this, sizeof(OVERLAPPED));
        }
    };

    struct overlapped_runtime_event final : webcraft::async::detail::runtime_event
    {
    private:
        overlapped_event *overlapped;
        HANDLE iocp;

    public:
        overlapped_runtime_event(std::stop_token token = webcraft::async::get_stop_token()) : runtime_event(token)
        {
            overlapped = new overlapped_event(this);
            iocp = reinterpret_cast<HANDLE>(webcraft::async::detail::get_native_handle());
        }

        ~overlapped_runtime_event()
        {
            delete overlapped;
        }

        void try_native_cancel() override
        {
            BOOL result = CancelIoEx(iocp, overlapped);
            if (!result && GetLastError() != ERROR_NOT_FOUND)
            {
                throw std::runtime_error("Failed to cancel IO operation: " + std::to_string(GetLastError()));
            }
        }

        void try_start() override
        {
            BOOL result = perform_overlapped_operation(iocp, overlapped);
            if (!result)
            {
                throw std::runtime_error("Failed to post overlapped event: " + std::to_string(GetLastError()));
            }
        }

        virtual BOOL perform_overlapped_operation(HANDLE iocp, LPOVERLAPPED overlapped) = 0;
    };
}

#endif