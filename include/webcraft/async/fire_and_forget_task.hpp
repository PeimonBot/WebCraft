#pragma once

#include <coroutine>

namespace webcraft::async
{

    class fire_and_forget_task
    {
    public:
        class promise_type
        {
        public:
            fire_and_forget_task get_return_object()
            {
                return {};
            }

            std::suspend_never initial_suspend() noexcept { return {}; }
            std::suspend_never final_suspend() noexcept { return {}; }

            void return_void() noexcept {}
            void unhandled_exception() noexcept {}
        };
    };

    inline fire_and_forget_task fire_and_forget(task<void> t)
    {
        co_await t;
        co_return;
    }
}