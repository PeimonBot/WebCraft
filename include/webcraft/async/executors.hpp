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
#include <deque>
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

        task<void> schedule()
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
        using executor::schedule;

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
        using executor_service::schedule;

        thread_per_task() {}

        void schedule(std::function<void()> fn) override
        {
            std::thread(fn).detach();
        }
    };

    class thread_pool : public executor_service
    {
    public:
        using executor_service::schedule;

        struct worker
        {
            std::jthread thr;

            worker() = default;
            virtual ~worker()
            {
                stop();
            }

            virtual void runLoop(std::stop_token stop_token) = 0;

            void start()
            {
                thr = std::jthread([&](std::stop_token t)
                                   { runLoop(t); });
            }

            void stop()
            {
                thr.request_stop();
            }
        };

    protected:
        std::vector<std::unique_ptr<worker>> workers;

    public:
        thread_pool() {}
        ~thread_pool()
        {
            stop();
        }

        virtual std::vector<std::unique_ptr<worker>> createWorkers() = 0;

        void start()
        {
            workers = createWorkers();
            for (auto &worker : workers)
            {
                worker->start();
            }
        }

        void stop()
        {
            for (auto &worker : workers)
            {
                worker->stop();
            }
        }
    };

    class fixed_size_thread_pool : public thread_pool
    {
    public:
        using thread_pool::schedule;

        struct worker : public thread_pool::worker
        {
            fixed_size_thread_pool &pool;

            worker(fixed_size_thread_pool &pool) : pool(pool) {}

            void runLoop(std::stop_token stop_token) override
            {
                while (!stop_token.stop_requested())
                {
                    std::function<void()> task;

                    {
                        std::unique_lock<std::mutex> lock(pool.mutex);
                        pool.condition.wait(lock, [this, stop_token]
                                            { return !pool.tasks.empty() || stop_token.stop_requested(); });

                        if (stop_token.stop_requested())
                            return;

                        if (!pool.tasks.empty())
                        {
                            task = std::move(pool.tasks.front());
                            pool.tasks.pop_front();
                        }
                    }

                    if (task)
                    {
                        task();
                    }
                }
            }
        };

        std::mutex mutex;
        std::condition_variable condition;
        std::deque<std::function<void()>> tasks;
        size_t num_workers;

        fixed_size_thread_pool(size_t num_workers = std::thread::hardware_concurrency()) : num_workers(num_workers)
        {
            workers.resize(num_workers);
        }

        ~fixed_size_thread_pool()
        {
            stop();
            condition.notify_all();
        }

        std::vector<std::unique_ptr<thread_pool::worker>> createWorkers() override
        {
            std::vector<std::unique_ptr<thread_pool::worker>> created_workers;
            created_workers.reserve(num_workers);
            for (size_t i = 0; i < num_workers; ++i)
            {
                created_workers.emplace_back(std::make_unique<worker>(*this));
            }
            return created_workers;
        }

        void schedule(std::function<void()> fn) override
        {
            std::unique_lock<std::mutex> lock(mutex);
            tasks.emplace_back(fn);
            condition.notify_one();
        }
    };
}
