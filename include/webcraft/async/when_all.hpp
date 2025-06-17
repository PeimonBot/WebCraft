#pragma once
#include <vector>
#include <optional>
#include <ranges>
#include <memory>
#include <atomic>

#include <async/awaitable_resume_t.h>

#include <webcraft/async/awaitable.hpp>
#include <webcraft/concepts.hpp>

namespace webcraft::async
{

    /// @brief Executes all the tasks concurrently and returns the result of all tasks in the submitted order
    /// @tparam range the range of the view
    /// @param tasks the tasks to execute
    /// @return the result of all tasks in the submitted order
    template <std::ranges::input_range range>
        requires webcraft::not_same_as<task<void>, std::ranges::range_value_t<range>> && awaitable<std::ranges::range_value_t<range>>
    auto when_all(range &&tasks) -> task<std::vector<::async::awaitable_resume_t<std::ranges::range_value_t<range>>>>
    {
        using Task = std::ranges::range_value_t<range>;
        using T = ::async::awaitable_resume_t<Task>;

        std::vector<std::optional<T>> results;
        std::vector<task<void>> wrappers;

        if constexpr (std::ranges::sized_range<range>)
        {
            results.reserve(std::ranges::size(tasks));
            wrappers.reserve(std::ranges::size(tasks));
        }

        for (auto &&task : tasks)
        {
            auto &slot = results.emplace_back(std::nullopt);

            struct fn_wrapper
            {

                webcraft::async::task<void> operator()(Task &&t, std::optional<T> &slot) const
                {
                    // This lambda will be used to store the result of the task in the slot
                    slot = co_await t;
                }
            };

            fn_wrapper fn;
            // Create a wrapper task that will store the result in the slot
            wrappers.emplace_back(fn(std::forward<Task>(task), slot));
        }

        co_await when_all(wrappers);

        auto pipeline = results | std::views::transform([](const std::optional<T> &opt)
                                                        {
                if (opt.has_value())
                    return opt.value();
                throw std::runtime_error("Task did not return a value"); });

        std::vector<T> out;
        if constexpr (std::ranges::sized_range<range>)
        {
            out.reserve(std::ranges::size(pipeline));
        }
        std::ranges::copy(pipeline, std::back_inserter(out));
        co_return out;
    }

    /// @brief Executes all the tasks concurrently
    /// @tparam range the range of the view
    /// @param tasks the tasks to execute
    /// @return an awaitable
    template <std::ranges::input_range range>
        requires std::same_as<task<void>, std::ranges::range_value_t<range>>
    task<void> when_all(range tasks)
    {
        for (auto &&handle : tasks)
            co_await handle;
    }
}