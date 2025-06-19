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

    namespace details
    {
        struct task_vector_tag
        {
        };
        struct task_result_tag
        {
        };

        // Await and discard result
        template <std::ranges::input_range Range>
        task<void> when_all_impl(Range &&tasks, task_vector_tag)
        {
            for (auto &&t : tasks)
                co_await t;
        }

        // Await and collect result
        template <std::ranges::input_range Range, typename R = std::ranges::range_value_t<std::remove_cvref_t<Range>>>
        auto when_all_impl(Range &&tasks, task_result_tag)
            -> task<std::vector<::async::awaitable_resume_t<R>>>
        {
            using Task = R;
            using T = ::async::awaitable_resume_t<Task>;

            std::vector<std::optional<T>> results;
            std::vector<task<void>> wrappers;

            if constexpr (std::ranges::sized_range<Range>)
            {
                results.reserve(std::ranges::size(tasks));
                wrappers.reserve(std::ranges::size(tasks));
            }

            for (auto &&t : tasks)
            {
                auto &slot = results.emplace_back(std::nullopt);

                struct task_wrapper
                {
                    task<T> t;
                    std::optional<T> &slot;

                    task<void> operator()()
                    {
                        slot = co_await t;
                    }
                };

                task_wrapper wrapper{std::move(t), slot};

                wrappers.emplace_back(wrapper());
            }

            co_await when_all_impl(std::move(wrappers), task_vector_tag{});

            co_return std::vector<T>(results | std::views::transform([](const auto &opt) -> T
                                                                     {
                if (opt.has_value())
                    return *opt;
                else
                    throw std::runtime_error("Task result is not available"); }));
        }
    }

    /// @brief Executes all the tasks concurrently and returns the result of all tasks in the submitted order
    /// @tparam range the range of the view
    /// @param tasks the tasks to execute
    /// @return the result of all tasks in the submitted order
    template <std::ranges::input_range Range>
    auto when_all(Range &&tasks)
    {
        using Task = std::ranges::range_value_t<std::remove_cvref_t<Range>>;

        if constexpr (std::same_as<Task, task<void>>)
            return details::when_all_impl(std::forward<Range>(tasks), details::task_vector_tag{});
        else
            return details::when_all_impl(std::forward<Range>(tasks), details::task_result_tag{});
    }
}