#pragma once

#ifdef _WIN32

#include <functional>
#include <chrono>
#include <queue>
#include <vector>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

struct data_entry
{
    std::chrono::steady_clock::time_point time_point;
    std::function<void()> callback;
    ;

    bool operator>(const data_entry &other) const
    {
        return time_point > other.time_point;
    }
};

void CALLBACK TimerCallback(PTP_CALLBACK_INSTANCE, PVOID context, PTP_TIMER);

class timer_manager
{
private:
    std::priority_queue<data_entry,
                        std::vector<data_entry>,
                        std::greater<>>
        timers;

    HANDLE iocp;
    PTP_TIMER thread_pool_timer;

    friend void CALLBACK TimerCallback(PTP_CALLBACK_INSTANCE, PVOID context, PTP_TIMER);

private:
    void process_timers()
    {
        auto now = std::chrono::steady_clock::now();
        while (!timers.empty())
        {
            if (timers.top().time_point <= now)
            {
                auto entry = timers.top();
                timers.pop();
                entry.callback();
            }
            else
            {
                break; // No more timers to process
            }
        }

        if (!timers.empty())
        {
            auto next_time_point = timers.top().time_point;
            auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(next_time_point - now);
            set_timer(duration);
        }
    }

    void set_timer(std::chrono::steady_clock::duration duration)
    {
        FILETIME ft;
        auto duration_nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(duration) / 100;
        ft.dwLowDateTime = static_cast<DWORD>(duration_nanos.count() & 0xFFFFFFFF);
        ft.dwHighDateTime = static_cast<DWORD>(duration_nanos.count() >> 32);

        SetThreadpoolTimer(thread_pool_timer, &ft, 0, 0);
    }

public:
    timer_manager(HANDLE iocp_handle)
        : iocp(iocp_handle), thread_pool_timer(nullptr)
    {
        thread_pool_timer = CreateThreadpoolTimer(TimerCallback, this, nullptr);
        printf("Created thread pool timer: %p\n", thread_pool_timer);

        if (thread_pool_timer == nullptr)
        {
            throw std::runtime_error("Failed to create thread pool timer");
        }
    }

    ~timer_manager()
    {
        SetThreadpoolTimer(thread_pool_timer, nullptr, 0, 0);
        WaitForThreadpoolTimerCallbacks(thread_pool_timer, TRUE);
        CloseThreadpoolTimer(thread_pool_timer);
    }

    void post_timer_event(std::chrono::steady_clock::duration duration, std::function<void()> callback)
    {
        auto time_point = std::chrono::steady_clock::now() + duration;

        set_timer(duration);
        timers.push({time_point, callback});
        std::cout << "Posted timer event for duration: " << duration.count() << " nanoseconds" << std::endl;
    }
};

void CALLBACK TimerCallback(PTP_CALLBACK_INSTANCE, PVOID context, PTP_TIMER)
{
    auto *manager = static_cast<timer_manager *>(context);
    manager->process_timers();
}

#endif