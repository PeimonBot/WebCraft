#pragma once

#include <coroutine>
#include <webcraft/async/async_event.hpp>
#include <webcraft/async/when_all.hpp>

namespace webcraft::async
{
    template <awaitable_t Awaitable>
        requires std::same_as<awaitable_resume_t<Awaitable>, void>
    task<void> wait_and_trigger(Awaitable &&awaitable, async_event &evt, std::atomic<std::size_t> &winner, std::size_t index)
    {
        co_await awaitable;
        std::size_t expected = -1;
        if (winner.compare_exchange_strong(expected, index, std::memory_order_acq_rel))
        {
            evt.set(); // Notify the event
        }
        co_return;
    }

    template <std::ranges::input_range Range,
              typename T = std::ranges::range_value_t<Range>,
              typename Result = awaitable_resume_t<T>>
        requires awaitable_t<T> && std::same_as<Result, void>
    task<void> when_any(Range &&tasks)
    {
        async_event event;
        std::atomic<std::size_t> winner = -1;

        std::vector<task<void>> wait_tasks;

        size_t index = 0;
        for (auto &&task : tasks)
        {
            wait_tasks.emplace_back(wait_and_trigger(std::forward<decltype(task)>(task), event, winner, index++));
        }

        co_await event;
    }

    template <awaitable_t Awaitable, typename Result = awaitable_resume_t<Awaitable>>
        requires(!std::same_as<Result, void>)
    task<void> wait_and_trigger(Awaitable &&awaitable, async_event &evt, std::atomic<std::size_t> &winner, std::optional<Result> &opt, std::size_t index)
    {
        auto value = co_await awaitable;
        std::size_t expected = -1;
        if (winner.compare_exchange_strong(expected, index, std::memory_order_acq_rel))
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
    task<Result> when_any(Range &&tasks)
    {
        async_event event;
        std::atomic<std::size_t> winner = -1;

        std::vector<task<void>> wait_tasks;
        std::optional<Result> result;

        size_t index = 0;
        for (auto &&task : tasks)
        {
            wait_tasks.emplace_back(wait_and_trigger(std::forward<decltype(task)>(task), event, winner, result, index++));
        }

        co_await event;

        if (winner.load(std::memory_order_acquire) != -1)
        {
            co_return result.value();
        }

        throw std::runtime_error("No task completed successfully");
    }

    template <typename... Tasks>
        requires(awaitable_t<Tasks> && ...)
    task<std::variant<normalized_result_t<Tasks>...>> when_any(std::tuple<Tasks...> tasks)
    {
        using result_variant_t = std::variant<normalized_result_t<Tasks>...>;

        auto evt = std::make_shared<async_event>();
        auto result = std::make_shared<std::optional<result_variant_t>>();
        auto done = std::make_shared<std::atomic<bool>>(false);

        auto await_one = [evt, result, done]<std::size_t I>(auto &&t) -> task<void>
        {
            using Res = normalized_result_t<std::tuple_element_t<I, std::tuple<Tasks...>>>;

            if constexpr (std::same_as<Res, std::monostate>)
            {
                co_await t;
                if (!done->exchange(true))
                {
                    *result = result_variant_t{std::in_place_index<I>, std::monostate{}};
                    evt->set();
                }
            }
            else
            {
                auto r = co_await t;
                if (!done->exchange(true))
                {
                    *result = result_variant_t{std::in_place_index<I>, std::move(r)};
                    evt->set();
                }
            }

            co_return;
        };

        // Launch all sub-tasks
        std::vector<task<void>> tasks_vector;
        [&]<std::size_t... Is>(std::index_sequence<Is...>)
        {
            (tasks_vector.emplace_back(await_one.template operator()<Is>(std::get<Is>(tasks))), ...);
        }(std::make_index_sequence<sizeof...(Tasks)>{});

        co_await *evt;
        co_return std::move(result->value());
    }
}