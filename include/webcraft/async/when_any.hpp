#pragma once

#include <coroutine>
#include <webcraft/async/async_event.hpp>
#include <webcraft/async/when_all.hpp>

namespace webcraft::async
{
    namespace details
    {

        struct any_result_tag
        {
        };
        struct any_void_tag
        {
        };

        template <std::ranges::input_range Range, typename R = std::ranges::range_value_t<std::remove_cvref_t<Range>>>
        auto when_any_impl(Range &&tasks, any_result_tag)
            -> task<awaitable_resume_t<R>>
        {
            using Task = R;
            using T = awaitable_resume_t<Task>;

            std::optional<T> result;
            std::atomic<bool> flag{false};
            async_event ev;

            std::vector<task<void>> wrapped_tasks;
            if constexpr (std::ranges::sized_range<Range>)
                wrapped_tasks.reserve(std::ranges::size(tasks));

            for (auto &&t : tasks)
            {
                struct task_wrapper
                {
                    task<T> t;
                    std::atomic<bool> &flag;
                    std::optional<T> &result;
                    async_event &ev;

                    task<void> operator()()
                    {
                        T value = co_await t;
                        bool expected = false;
                        if (flag.compare_exchange_strong(expected, true, std::memory_order_relaxed))
                        {
                            result = std::move(value);
                            ev.set();
                        }
                    }
                };

                task_wrapper wrapper{std::move(t), flag, result, ev};

                wrapped_tasks.emplace_back(wrapper());
            }

            co_await ev;
            co_return std::move(*result); // safe due to `ev`
        }

        template <std::ranges::input_range Range>
        task<void> when_any_impl(Range &&tasks, any_void_tag)
        {
            std::atomic<bool> flag{false};
            async_event ev;

            std::vector<task<void>> wrapped_tasks;
            if constexpr (std::ranges::sized_range<Range>)
                wrapped_tasks.reserve(std::ranges::size(tasks));

            for (auto &&t : tasks)
            {

                struct task_wrapper
                {
                    task<void> t;
                    std::atomic<bool> &flag;
                    async_event &ev;

                    task<void> operator()()
                    {
                        co_await t;
                        bool expected = false;
                        if (flag.compare_exchange_strong(expected, true, std::memory_order_relaxed))
                        {
                            ev.set();
                        }
                    }
                };

                task_wrapper wrapper{std::move(t), flag, ev};

                wrapped_tasks.emplace_back(wrapper());
            }

            co_await ev;
        }
    }

    /// @brief Executes all the tasks concurrently and returns the first one which finishes and either cancels or discards the other tasks (once first one complete, the other tasks need not complete)
    /// @tparam ...Rets the return arguments of the tasks
    /// @param tasks the tasks to execute
    /// @return the result of the first task to finish
    template <std::ranges::input_range Range>
    auto when_any(Range &&tasks)
    {
        using Task = std::ranges::range_value_t<std::remove_cvref_t<Range>>;

        if constexpr (std::same_as<Task, task<void>>)
            return details::when_any_impl(std::forward<Range>(tasks), details::any_void_tag{});
        else
            return details::when_any_impl(std::forward<Range>(tasks), details::any_result_tag{});
    }

}