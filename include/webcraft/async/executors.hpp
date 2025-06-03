#pragma once

#include <async/awaitable_resume_t.h>
#include <webcraft/concepts.hpp>
#include <webcraft/async/awaitable.hpp>
#include <webcraft/async/runtime.hpp>
#include <ranges>

namespace webcraft::async
{
    /// @brief The priority of the task to be scheduled
    enum class scheduling_priority
    {
        LOW,
        HIGH
    };

    /// @brief the parameters to initialize the executor service
    struct executor_service_params
    {
        size_t minWorkers;
        size_t maxWorkers;
        std::chrono::milliseconds idleTimeout;
        worker_strategy_type strategy;
    };

    /// @brief the executor service strategy
    class executor
    {
    public:
        /// Schedules the current coroutine onto the thread pool
        virtual task<void> schedule(scheduling_priority priority = scheduling_priority::LOW) = 0;
    };

    /// @brief A class that represents an executor service that can be used to run tasks asynchronously.
    class executor_service final
    {
    private:
        friend class async_runtime;
        async_runtime &runtime;
        std::unique_ptr<executor> strategy; // strategy for the executor service

#pragma region "constructors and destructors"
    protected:
        executor_service(async_runtime &runtime, executor_service_params &params);

    public:
        ~executor_service();
        executor_service(const executor_service &) = delete;
        executor_service(executor_service &&) = delete;
        executor_service &operator=(const executor_service &) = delete;
        executor_service &operator=(executor_service &&) = delete;
#pragma endregion

#pragma region "scheduling"

        /// @brief Schedules the current coroutine onto the executor
        /// @param priority the priority the coroutine should run at
        /// @return an awaitable
        inline task<void> schedule(scheduling_priority priority = scheduling_priority::LOW)
        {
            return strategy->schedule(priority);
        }

        /// @brief Schedules the current coroutine onto the executor with a low priority
        /// @return an awaitable
        inline task<void> schedule_low()
        {
            return schedule(scheduling_priority::LOW);
        }

        /// @brief Schedules the current coroutine onto the executor with a high priority
        /// @return an awaitable
        inline task<void> schedule_high()
        {
            return schedule(scheduling_priority::HIGH);
        }

        template <typename Fn, typename... Args>
            requires awaitable<std::invoke_result_t<Fn, Args...>>
        auto schedule(scheduling_priority priority, Fn &&fn, Args &&...args)
        {
            using T = ::async::awaitable_resume_t<std::remove_cvref_t<std::invoke_result_t<Fn, Args...>>>;

            auto fn_ = [this, priority](Fn &&_fn, Args &&..._args) -> task<T>
            {
                co_await schedule(priority);

                if constexpr (std::is_void_v<T>)
                {
                    // If the result type is void, we can just co_await the function
                    co_await std::invoke(std::forward<Fn>(_fn), std::forward<Args>(_args)...);
                    co_return;
                }
                else
                {
                    co_return co_await std::invoke(std::forward<Fn>(_fn), std::forward<Args>(_args)...);
                }
            };

            return task<T>(fn_(std::forward<Fn>(fn), std::forward<Args>(args)...));
        }

        template <typename Fn, typename... Args>
            requires awaitable<std::invoke_result_t<Fn, Args...>>
        auto schedule_low(Fn &&fn, Args &&...args)
        {
            return schedule(scheduling_priority::LOW, std::forward<Fn>(fn), std::forward<Args>(args)...);
        }

        template <typename Fn, typename... Args>
            requires awaitable<std::invoke_result_t<Fn, Args...>>
        auto schedule_high(Fn &&fn, Args &&...args)
        {
            return schedule(scheduling_priority::HIGH, std::forward<Fn>(fn), std::forward<Args>(args)...);
        }

#pragma endregion

#pragma region "parallel processing"
        /// @brief Runs the tasks in parallel
        /// @param tasks the tasks to run in parallel
        /// @return an awaitable
        template <std::ranges::range range>
            requires std::convertible_to<std::ranges::range_value_t<range>, std::function<task<void>()>>
        inline task<void> runParallel(range tasks)
        {
            struct fn_awaiter
            {
                executor_service &svc;

                task<void> operator()(std::function<task<void>()> fn) const
                {
                    co_await svc.schedule();
                    co_return co_await fn();
                }
            };

            // schedule and join the tasks
            co_await runtime.when_all(tasks | std::ranges::transform(
                                                  [&](auto &&task_fn)
                                                  {
                                                      fn_awaiter fn{*this};
                                                      return fn();
                                                  }));
        }

        template <std::ranges::range range>
            requires webcraft::not_same_as<task<void>, std::ranges::range_value_t<range>> && awaitable<std::ranges::range_value_t<range>>
        auto runParallel(range tasks) -> task<std::vector<::async::awaitable_resume_t<std::ranges::range_value_t<range>>>>
        {
            using T = ::async::awaitable_resume_t<std::ranges::range_value_t<range>>;

            // schedule each task and immediately collect their handles
            // join the handles and return the result
            std::vector<std::optional<T>> vec;

            // Assign each task a return destination, schedule them all and join them
            co_await runtime.when_all(tasks | std::ranges::transform(
                                                  [&](task<T> t)
                                                  {
                                                      vec.push_back({std::nullopt});
                                                      size_t index = vec.size() - 1;

                                                      auto fn = [vec, index](task<T> t) -> task<T>
                                                      {
                                                          vec[index] = co_await t;
                                                      };

                                                      // schedule the tasks
                                                      return schedule(fn(t));
                                                  }));

            auto pipe = vec | std::views::transform([](std::optional<T> opt)
                                                    { return opt.value(); });
            std::vector<T> out;
            out.reserve(std::ranges::distance(pipe));
            std::ranges::copy(pipe, std::back_inserter(out));
            co_return out;
        }
#pragma endregion
    };
}
