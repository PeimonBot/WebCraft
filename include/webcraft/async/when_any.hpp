#pragma once

#include <coroutine>
#include <webcraft/async/when_all.hpp>

namespace webcraft::async
{
    namespace internal
    {
        struct async_event
        {
            std::vector<std::coroutine_handle<>> handles;

            constexpr bool await_ready() { return false; }
            constexpr void await_suspend(std::coroutine_handle<> h)
            {
                this->handles.push_back(h);
            }
            constexpr void await_resume() {}

            void set()
            {
                // resumes the coroutine
                for (auto &h : this->handles)
                {
                    if (!h.done())
                    {
                        h.resume();
                    }
                }
            }
        };
    }

    /// @brief Executes all the tasks concurrently and returns the first one which finishes and either cancels or discards the other tasks (once first one complete, the other tasks need not complete)
    /// @tparam ...Rets the return arguments of the tasks
    /// @param tasks the tasks to execute
    /// @return the result of the first task to finish
    template <std::ranges::input_range range>
        requires webcraft::not_same_as<task<void>, std::ranges::range_value_t<range>> && awaitable<std::ranges::range_value_t<range>>
    auto when_any(range tasks) -> task<::async::awaitable_resume_t<std::ranges::range_value_t<range>>>
    {
        // I know conceptually how it works but the atomic operations.... idk
        using T = ::async::awaitable_resume_t<std::ranges::range_value_t<range>>;

        std::optional<T> value;
        std::atomic<bool> flag;

        // Create an event which will resume the coroutine when the event is set
        std::vector<task<void>> tasks_to_run;

        if constexpr (std::ranges::sized_range<range>)
        {
            tasks_to_run.reserve(std::ranges::size(tasks));
        }

        struct op
        {

            task<void> operator()(task<T> &&t, std::atomic<bool> &flag, std::optional<T> &value) const
            {
                // This lambda will be used to store the result of the task in the slot
                T result = co_await t;

                // only set the value if the flag is not set
                bool check = false;
                if (flag.compare_exchange_strong(check, true, std::memory_order_relaxed))
                {
                    value = std::move(result);
                }
            }
        }

        // go through all the tasks and spawn them
        for (auto t : tasks)
        {
            op fn;

            // Create a wrapper task that will store the result in the slot
            tasks_to_run.push_back(fn(std::move(t), flag, value));
        }

        // wait for the event to be set
        co_await when_any(tasks_to_run);

        // return the value
        co_return value.value();
    }

    /// @brief Executes all the tasks concurrently and returns the first one which finishes and either cancels or discards the other tasks (once first one complete, the other tasks need not complete)
    /// @tparam ...Rets the return arguments of the tasks
    /// @param tasks the tasks to execute
    /// @return the result of the first task to finish
    template <std::ranges::input_range range>
        requires std::same_as<task<void>, std::ranges::range_value_t<range>>
    auto when_any(range tasks) -> task<void>
    {
        // I know conceptually how it works but the atomic operations.... idk
        std::atomic<bool> flag;

        // create teh event
        internal::async_event ev;
        std::vector<task<void>> tasks_to_run;

        struct op
        {

            task<void> operator()(task<void> &&t, std::atomic<bool> &flag, async_event &ev) const
            {
                // This lambda will be used to set the event when the task is done
                co_await t;

                // only set the event if the flag is not set
                bool check = false;
                if (flag.compare_exchange_strong(check, true, std::memory_order_relaxed))
                {
                    ev.set();
                }
            }
        }

        if constexpr (std::ranges::sized_range<range>)
        {
            tasks_to_run.reserve(std::ranges::size(tasks));
        }

        // go through all the tasks and spawn them
        for (auto t : tasks)
        {
            op fn;

            tasks_to_run.push_back(fn(std::move(t), flag, ev));
        }

        // wait for the event to be set
        co_await ev;
    }
}