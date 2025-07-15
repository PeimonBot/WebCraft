#pragma once

#include <coroutine>
#include <webcraft/async/async_event.hpp>
#include <webcraft/async/when_all.hpp>

namespace webcraft::async
{
    namespace detail
    {

        class self_deleting_task
        {
        public:
            class promise_type
            {
            public:
                self_deleting_task get_return_object()
                {
                    return self_deleting_task{this};
                }

                std::suspend_always initial_suspend() noexcept { return {}; }
                std::suspend_never final_suspend() noexcept { return {}; }

                void return_void() noexcept {}
                void unhandled_exception() noexcept {}
            };

            self_deleting_task(promise_type *p) : m_promise(p) {}

            auto promise() const noexcept
            {
                return *m_promise;
            }

            bool resume()
            {
                auto h = std::coroutine_handle<promise_type>::from_promise(*m_promise);
                if (!h.done())
                {
                    h.resume();
                }
                return !h.done();
            }

        private:
            promise_type *m_promise = nullptr;
        };

        template <awaitable_t Awaitable>
            requires std::same_as<awaitable_resume_t<Awaitable>, void>
        task<void> wait_and_trigger(Awaitable &&awaitable, async_event &evt, std::atomic<bool> &winner)
        {
            co_await awaitable;
            bool expected = false;
            if (winner.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            {
                evt.set(); // Notify the event
            }
            co_return;
        }

        template <std::ranges::input_range Range,
                  typename T = std::ranges::range_value_t<Range>,
                  typename Result = awaitable_resume_t<T>>
            requires awaitable_t<T> && std::same_as<Result, void>
        self_deleting_task when_any_controller(Range &&tasks, async_event &event)
        {
            std::atomic<bool> winner = false;
            std::vector<task<void>> wait_tasks;

            for (auto &&task : tasks)
            {
                wait_tasks.emplace_back(wait_and_trigger(std::forward<decltype(task)>(task), event, winner));
            }

            co_await when_all(wait_tasks);
            co_return;
        }

        template <awaitable_t Awaitable, typename Result = awaitable_resume_t<Awaitable>>
            requires(!std::same_as<Result, void>)
        task<void> wait_and_trigger(Awaitable &&awaitable, async_event &evt, std::atomic<bool> &winner, std::optional<Result> &opt)
        {
            auto value = co_await awaitable;
            bool expected = false;
            if (winner.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            {
                opt = std::forward<Result>(value);
                evt.set(); // Notify the event
            }
            co_return;
        }

        template <std::ranges::input_range Range,
                  typename T = std::ranges::range_value_t<Range>,
                  typename Result = awaitable_resume_t<T>>
            requires awaitable_t<T> && (!std::same_as<Result, void>)
        self_deleting_task when_any_controller(Range &&tasks, async_event &event, std::optional<Result> &opt)
        {
            std::atomic<bool> winner = false;
            std::vector<task<void>> wait_tasks;

            for (auto &&task : tasks)
            {
                wait_tasks.emplace_back(wait_and_trigger(std::forward<decltype(task)>(task), event, winner, opt));
            }

            co_await when_all(wait_tasks);
            co_return;
        }
    }

    template <std::ranges::input_range Range,
              typename T = std::ranges::range_value_t<Range>,
              typename Result = awaitable_resume_t<T>>
        requires awaitable_t<T>
    task<Result> when_any(Range &&tasks)
    {
        async_event event;

        if constexpr (std::is_void_v<Result>)
        {
            auto task = detail::when_any_controller(std::forward<Range>(tasks), event);
            task.resume();

            co_await event;
            co_return;
        }
        else
        {
            std::optional<Result> result;

            auto task = detail::when_any_controller(std::forward<Range>(tasks), event, result);
            task.resume();
            co_await event;

            co_return result.value();
        }
    }

    // template <typename... Tasks>
    //     requires(awaitable_t<Tasks> && ...)
    // task<std::variant<normalized_result_t<Tasks>...>> when_any(std::tuple<Tasks...> tasks)
    // {
    //     using result_variant_t = std::variant<normalized_result_t<Tasks>...>;

    //     auto evt = std::make_shared<async_event>();
    //     auto result = std::make_shared<std::optional<result_variant_t>>();
    //     auto done = std::make_shared<std::atomic<bool>>(false);

    //     auto await_one = [evt, result, done]<std::size_t I>(auto &&t) -> task<void>
    //     {
    //         using Res = normalized_result_t<std::tuple_element_t<I, std::tuple<Tasks...>>>;

    //         if constexpr (std::same_as<Res, std::monostate>)
    //         {
    //             co_await t;
    //             if (!done->exchange(true))
    //             {
    //                 *result = result_variant_t{std::in_place_index<I>, std::monostate{}};
    //                 evt->set();
    //             }
    //         }
    //         else
    //         {
    //             auto r = co_await t;
    //             if (!done->exchange(true))
    //             {
    //                 *result = result_variant_t{std::in_place_index<I>, std::move(r)};
    //                 evt->set();
    //             }
    //         }

    //         co_return;
    //     };

    //     // Launch all sub-tasks
    //     std::vector<task<void>> tasks_vector;
    //     [&]<std::size_t... Is>(std::index_sequence<Is...>)
    //     {
    //         (tasks_vector.emplace_back(await_one.template operator()<Is>(std::get<Is>(tasks))), ...);
    //     }(std::make_index_sequence<sizeof...(Tasks)>{});

    //     co_await *evt;
    //     co_return std::move(result->value());
    // }
}