#pragma once

#include <webcraft/async/awaitable.hpp>
#include <thread>
#include <optional>
#include <webcraft/concepts.hpp>
#include <variant>
#include <algorithm>
#include <async/awaitable_resume_t.h>
#include <async/event_signal.h>
#include <ranges>
#include <webcraft/async/platform.hpp>

namespace webcraft::async
{
#pragma region "forward declarations"

    namespace io
    {
        class io_service;
    }
    class executor_service;
    class timer_service;
    class async_runtime;

#pragma endregion

    /// @brief Singleton-like object that manages and provides a runtime for async operations to occur.
    class async_runtime final
    {
    private:
#pragma region "friend classes"
        friend class io::io_service;
        friend class timer_service;

        std::unique_ptr<io::io_service> io_svc;
        std::unique_ptr<timer_service> timer_svc;
        ::async::event_signal ev;
#pragma endregion

#pragma region "runtime handle"
        runtime_handle handle; // platform specific runtime handle

    public:
        inline runtime_handle &get_runtime_handle()
        {
            return handle;
        }

    private:
#pragma endregion

#pragma region "constructors"
        async_runtime();
        async_runtime(const async_runtime &) = delete;
        async_runtime(async_runtime &&) = delete;
        async_runtime &operator=(const async_runtime &) = delete;
        async_runtime &operator=(async_runtime &&) = delete;
        ~async_runtime();
#pragma endregion

    public:
#pragma region "singleton initializer"
        /// @brief Get the singleton instance of the async_runtime.
        /// @return The singleton instance of the async_runtime.
        static async_runtime &get_instance();
#pragma endregion

#pragma region "AsyncRuntime.run"
        /// @brief Runs the asynchronous function provided and returns the result.
        /// @tparam T the type of the result of the task.
        /// @tparam ...Args the types of the arguments to the task.
        /// @param fn the function to run.
        /// @return the result of the task.
        template <class Fn, class... Args>
            requires awaitable<std::invoke_result_t<Fn, Args...>>
        auto run_async(Fn &&fn, Args &&...args)
        {
            using T = ::async::awaitable_resume_t<std::remove_cvref_t<std::invoke_result_t<Fn, Args...>>>;

            auto task = std::invoke(std::forward<Fn>(fn), std::forward<Args>(args)...);

            // If the task is a task<void>, we can just run it
            if constexpr (std::is_void_v<T>)
            {
                run(std::move(task));
                return;
            }
            else
            {
                return run(std::move(task));
            }
        };

        /// @brief Runs the task asynchronously and returns the result.
        /// @tparam T the type of the result of the task provided
        /// @param task the task to run.
        /// @return the result of the task.
        template <typename T>
            requires webcraft::not_true<std::is_void_v<T>>
        T run(task<T> &&t)
        {
            /// This is a bit of a hack, but we need to be able to return the result of the task
            std::optional<T> result;

            // We need to create a lambda that captures the result and returns a task<void>
            // that sets the result when the task is done.
            auto fn = [&result](task<T> t) -> task<void>
            {
                result = co_await t;
            };

            // Invoke the lambda and receive the task
            auto task_fn = fn(t);
            // Run the task and waits synchronously for it to finish
            run(task_fn);

            // Check if the result is valid - This should never happen since we are using task<T> and not task<void>
            // but we need to check for it just in case.
            if (!result.has_value())
            {
                throw std::runtime_error("task did not return a value");
            }
            // Get the result from the optional
            return result.value();
        }

        /// @brief Runs the task asynchronously.
        /// @param task the task to run
        void run(task<void> &&t);

#pragma endregion

    private:
        // internal resume coroutine implementation
        void queue_task_resumption(std::coroutine_handle<> h);

    public:
#pragma region "AsyncRuntime.shutdown"
        /// @brief Shuts down the async runtime and stops the loop and waits for all tasks to finish.
        void shutdown()
        {
            if (ev.is_set())
            {
                return;
            }
            this->ev.set();

            for (int i = 0; i < 10; i++)
            {
                queue_task_resumption(std::noop_coroutine());
            }
        }
#pragma endregion

#pragma region "AsyncRuntime.yield"
        /// @brief Yields the task to the caller and lets other tasks in the queue to resume before this one is resumed
        /// @return returns a task which can be awaited
        inline task<void> yield()
        {
            struct yield_awaiter
            {
            public:
                async_runtime &runtime;

                constexpr bool await_ready() { return false; }
                void await_suspend(std::coroutine_handle<> h)
                {
                    // queue the task for resumption
                    runtime.queue_task_resumption(h);
                }
                constexpr void await_resume() {}
            };

            co_await yield_awaiter{*this};
        }
#pragma endregion

#pragma region "asynchronous services"

        /// @brief Gets the io_service for the runtime.
        /// @return the IO service
        io::io_service &get_io_service();

        /// @brief Gets the timer_service for the runtime.
        /// @return the timer service
        timer_service &get_timer_service();
#pragma endregion
    };

    template <typename T>
    task<T> value_of(T &&val)
    {
        co_return std::forward<T>(val);
    }
}