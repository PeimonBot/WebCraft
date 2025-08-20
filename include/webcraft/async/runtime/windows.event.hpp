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

    struct overlapped_runtime_event : public webcraft::async::detail::runtime_event
    {
    private:
        overlapped_event *overlapped;
        HANDLE iocp;

    public:
        overlapped_runtime_event(HANDLE iocp, std::stop_token token) : runtime_event(token), iocp(iocp)
        {
            overlapped = new overlapped_event(this);
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

    using overlapped_operation = std::function<BOOL(HANDLE, LPOVERLAPPED)>;

    inline auto create_overlapped_event(HANDLE iocp, overlapped_operation op, std::stop_token token = get_stop_token())
    {
        struct overlapped_runtime_event_impl : public overlapped_runtime_event
        {
            overlapped_runtime_event_impl(HANDLE iocp, overlapped_operation op, std::stop_token token)
                : overlapped_runtime_event(iocp, token), op(std::move(op))
            {
            }

            BOOL perform_overlapped_operation(HANDLE iocp, LPOVERLAPPED overlapped) override
            {
                return op(iocp, overlapped);
            }

        private:
            overlapped_operation op;
        };

        return std::make_unique<overlapped_runtime_event_impl>(iocp, std::move(op), token);
    }
}

#endif