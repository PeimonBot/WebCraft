#pragma once

///////////////////////////////////////////////////////////////////////////////
// Copyright (c) Aditya Rao
// Licenced under MIT license. See LICENSE.txt for details.
///////////////////////////////////////////////////////////////////////////////

#ifdef _WIN32

#include <functional>
#include <chrono>
#include <queue>
#include <vector>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace webcraft::async::runtime::detail
{

    class timer_manager;

    struct data_entry
    {
        std::function<void()> callback;
        timer_manager *manager;

        data_entry(std::function<void()> cb, timer_manager *mgr)
            : callback(std::move(cb)), manager(mgr) {}
    };

    class windows_timer_manager_error : public std::exception
    {
    public:
        explicit windows_timer_manager_error(std::string message) : msg_(message) {}
        virtual const char *what() const noexcept override
        {
            return msg_.c_str();
        }

    private:
        std::string msg_;
    };

    class timer_manager
    {

    private:
        void set_timer(PTP_TIMER timer, std::chrono::steady_clock::duration duration)
        {
            auto duration_nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(duration) / 100;

            LARGE_INTEGER li;
            li.QuadPart = -static_cast<LONGLONG>(duration_nanos.count()); // Convert to negative for relative time

            FILETIME ft;
            ft.dwLowDateTime = static_cast<DWORD>(li.LowPart);
            ft.dwHighDateTime = static_cast<DWORD>(li.HighPart);

            SetThreadpoolTimer(timer, &ft, 0, 0);
        }

        void create_timer(PTP_TIMER &timer, std::function<void()> callback)
        {
            timer = CreateThreadpoolTimer([](PTP_CALLBACK_INSTANCE, PVOID context, PTP_TIMER timer)
                                          { 
                                            data_entry* entry = static_cast<data_entry*>(context);
                                             entry->manager->process_timer(std::move(entry->callback), timer); },
                                          new data_entry(std::move(callback), this), nullptr);
            if (timer == nullptr)
            {
                throw windows_timer_manager_error("Failed to create thread pool timer");
            }
        }

        void close_timer(const PTP_TIMER &timer)
        {
            SetThreadpoolTimer(timer, nullptr, 0, 0);
            WaitForThreadpoolTimerCallbacks(timer, TRUE);
            CloseThreadpoolTimer(timer);
        }

        void process_timer(std::function<void()> callback, PTP_TIMER timer)
        {
            callback();
            close_timer(timer);
        }

    public:
        timer_manager()
        {
        }

        ~timer_manager()
        {
        }

        PTP_TIMER post_timer_event(std::chrono::steady_clock::duration duration, std::function<void()> callback)
        {
            auto time_point = std::chrono::steady_clock::now() + duration;
            PTP_TIMER timer;

            create_timer(timer, std::move(callback));
            set_timer(timer, duration);

            return timer;
        }

        void cancel_timer(const PTP_TIMER &timer)
        {
            if (timer)
            {
                close_timer(timer);
            }
        }
    };

}
#endif