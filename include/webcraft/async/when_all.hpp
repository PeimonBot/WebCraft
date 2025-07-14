#pragma once
#include <vector>
#include <optional>
#include <ranges>
#include <memory>
#include <atomic>
#include <utility>

#include <webcraft/async/task.hpp>
#include <webcraft/concepts.hpp>
#include <webcraft/ranges.hpp>
#include <variant>

namespace webcraft::async
{
    template <std::ranges::input_range Range,
              typename T = std::ranges::range_value_t<Range>,
              typename Result = awaitable_resume_t<T>>
        requires awaitable_t<T> && (!std::same_as<Result, void>)
    task<std::vector<Result>> when_all(Range &&tasks)
    {
        std::vector<Result> results;
        results.reserve(std::ranges::size(tasks));

        for (auto &&t : tasks)
        {
            results.push_back(co_await t);
        }

        co_return results;
    }

    template <std::ranges::input_range Range,
              typename T = std::ranges::range_value_t<Range>,
              typename Result = awaitable_resume_t<T>>
        requires awaitable_t<T> && std::same_as<Result, void>
    task<void> when_all(Range &&tasks)
    {
        for (auto &&t : tasks)
        {
            co_await t;
        }

        co_return;
    }

    template <typename T>
    using normalized_result_t = std::conditional_t<
        std::same_as<awaitable_resume_t<T>, void>,
        std::monostate,
        awaitable_resume_t<T>>;

    template <typename... Tasks>
        requires(awaitable_t<Tasks> && ...)
    task<std::tuple<normalized_result_t<Tasks>...>> when_all(std::tuple<Tasks...> tasks)
    {
        auto await_one = [](auto &&t) -> task<normalized_result_t<decltype(t)>>
        {
            if constexpr (std::same_as<awaitable_resume_t<decltype(t)>, void>)
            {
                co_await t;
                co_return std::monostate{};
            }
            else
            {
                co_return co_await t;
            }
        };

        auto await_many = [&]<std::size_t... Is>(std::index_sequence<Is...>) -> task<std::tuple<normalized_result_t<Tasks>...>>
        {
            co_return std::tuple<normalized_result_t<Tasks>...>{
                co_await await_one(std::get<Is>(tasks))...};
        };

        co_return co_await await_many(std::make_index_sequence<sizeof...(Tasks)>{});
    }
}