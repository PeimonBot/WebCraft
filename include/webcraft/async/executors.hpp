#pragma once

#include <async/awaitable_resume_t.h>
#include <webcraft/concepts.hpp>
#include <webcraft/async/awaitable.hpp>
#include <webcraft/async/runtime.hpp>
#include <webcraft/async/when_all.hpp>
#include <async/task_completion_source.h>
#include <functional>
#include <ranges>
#include <thread>
#include <future>

namespace webcraft::async
{
    /// @brief the executor service strategy
    class executor
    {
    public:
        executor() = default;
        virtual ~executor() = default;

        /// Schedules the current coroutine onto the thread pool
        virtual void schedule(std::function<void()> v) = 0;

        template <typename Fn, typename... Args>
        auto schedule(Fn &&fn, Args &&...args)
        {
            using T = std::invoke_result_t<Fn, Args...>;
            ::async::task_completion_source<T> promise;

            if constexpr (std::is_void_v<T>)
            {

                std::function<void()> delegate = [&promise, fn = std::forward<Fn>(fn), ... args = std::forward<Args>(args)]
                {
                    fn(args...);
                    promise.set_value();
                };

                schedule(delegate);
            }
            else
            {

                std::function<void()> delegate = [&promise, fn = std::forward<Fn>(fn), ... args = std::forward<Args>(args)]
                {
                    promise.set_value(fn(args...));
                };

                schedule(delegate);
            }

            return promise.task();
        }

        inline task<void> schedule()
        {

            struct dispatch_awaiter
            {
                executor *ptr;

                bool await_ready() { return false; }
                void await_suspend(std::coroutine_handle<> h)
                {
                    ptr->schedule([h]
                                  { h.resume(); });
                }
                void await_resume() {}
            };

            co_await dispatch_awaiter{this};
        }
    };

    /// @brief A class that represents an executor service that can be used to run tasks asynchronously.
    class executor_service : public executor
    {
#pragma region "constructors and destructors"
    public:
        executor_service() = default;
        virtual ~executor_service() = default;
        executor_service(const executor_service &) = delete;
        executor_service(executor_service &&) = delete;
        executor_service &operator=(const executor_service &) = delete;
        executor_service &operator=(executor_service &&) = delete;
#pragma endregion

#pragma region "parallel processing"
    private:
        template <std::invocable Callable, typename Ret = std::invoke_result_t<Callable>>
        struct fn_wrapper
        {
            executor_service *ptr;

            task<Ret> operator()(Callable &&callable)
            {
                co_await ptr->schedule();
                if constexpr (std::is_void_v<Ret>)
                {
                    callable();
                }
                else
                {
                    co_return callable();
                }
            }
        };

        template <std::invocable Callable, typename Awaitable = std::invoke_result_t<Callable>, typename Ret = ::async::awaitable_resume_t<Awaitable>>
        struct fn_awaitable_wrapper
        {
            executor_service *ptr;

            task<Ret> operator()(Callable &&callable)
            {
                co_await ptr->schedule();
                if constexpr (std::is_void_v<Ret>)
                {
                    co_await callable();
                }
                else
                {
                    co_return co_await callable();
                }
            }
        };

    public:
        // TODO: generate docs for this
        template <std::ranges::range range, typename Callable = std::ranges::range_value_t<range>>
            requires std::invocable<Callable>
        inline auto invoke_all(range tasks)
        {
            using Ret = std::invoke_result_t<Callable>;

            return when_all(tasks | std::views::transform([this](Callable &&r)
                                                          {
                    fn_wrapper<Callable> fn{ this };
                    return fn(std::forward<Callable>(r)); }));
        }

        // TODO: generate docs for this
        template <std::ranges::range range, typename Callable = std::ranges::range_value_t<range>>
            requires std::invocable<Callable> && awaitable<std::invoke_result_t<Callable>>
        inline auto invoke_await_all(range tasks)
        {
            using Task = std::invoke_result_t<Callable>;
            using Ret = ::async::awaitable_resume_t<Task>;

            return when_all(tasks | std::views::transform([this](Callable &&r)
                                                          {
                    fn_awaitable_wrapper<Callable> fn{ this };
                    return fn(std::forward<Callable>(r)); }));
        }

        // TODO: generate docs for this
        template <std::ranges::range range, typename Callable = std::ranges::range_value_t<range>>
            requires std::invocable<Callable>
        inline auto invoke_any(range tasks)
        {
            using Ret = std::invoke_result_t<Callable>;

            return when_any(tasks | std::views::transform([this](Callable &&r)
                                                          {
                    fn_wrapper<Callable> fn{ this };
                    return fn(std::forward<Callable>(r)); }));
        }

        // TODO: generate docs for this
        template <std::ranges::range range, typename Callable = std::ranges::range_value_t<range>>
            requires std::invocable<Callable> && awaitable<std::invoke_result_t<Callable>>
        inline auto invoke_await_any(range tasks)
        {
            using Task = std::invoke_result_t<Callable>;
            using Ret = ::async::awaitable_resume_t<Task>;

            return when_any(tasks | std::views::transform([this](Callable &&r)
                                                          {
                    fn_awaitable_wrapper<Callable> fn{ this };
                    return fn(std::forward<Callable>(r)); }));
        }
    };

    class thread_per_task : public executor_service
    {
    public:
        thread_per_task() {}

        void schedule(std::function<void()> fn) override
        {
            std::thread(fn).detach();
        }
    };
}
