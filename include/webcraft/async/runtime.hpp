#pragma once

#include <functional>
#include <chrono>
#include <stop_token>
#include <memory>

#include <webcraft/async/task.hpp>
#include <webcraft/async/sync_wait.hpp>

namespace webcraft::async
{

    namespace detail
    {
        void initialize_runtime() noexcept;

        void shutdown_runtime() noexcept;

        void post_yield_event(std::function<void()> func, int *result);

        void post_sleep_event(std::function<void()> func, std::chrono::steady_clock::duration duration, std::stop_token token, int *result);
    };

    /// @brief  Acts as a context for the async runtime, managing the lifecycle of async operations.
    /// This class is designed to be used as a singleton within the async runtime but won't be created like a singleton, more like a guard.
    class runtime_context final
    {
    public:
        runtime_context()
        {
            detail::initialize_runtime();
        }

        ~runtime_context()
        {
            detail::shutdown_runtime();
        }
        runtime_context(const runtime_context &) = delete;
        runtime_context &operator=(const runtime_context &) = delete;
        runtime_context(runtime_context &&) = delete;
        runtime_context &operator=(runtime_context &&) = delete;
    };

    /// @brief Gets the stop token for the current async runtime.
    /// @return the stop token associated with the async runtime.
    std::stop_token get_stop_token();

    /// @brief Yields control back to the async runtime, allowing other tasks to run.
    /// @return A task that completes when the yield operation is done.
    inline task<void> yield()
    {
        struct yield_awaiter
        {
            int result;
            std::exception_ptr exception;
            bool await_ready() const noexcept { return false; }
            void await_suspend(std::coroutine_handle<> h) noexcept
            {
                try
                {
                    detail::post_yield_event([h]
                                             { h.resume(); }, &result);
                }
                catch (...)
                {
                    exception = std::current_exception();
                    h.resume(); // Resume the coroutine even if an exception occurs
                }
            }
            void await_resume() noexcept
            {
                if (exception)
                {
                    std::rethrow_exception(exception);
                }
            }
        };
        co_await yield_awaiter{};
        co_return;
    }

    /// @brief Sleeps for a specified duration, allowing other tasks to run during the sleep.
    /// @param duration The duration to sleep.
    /// @param token The stop token to check for cancellation requests.
    /// @return A task that completes when the sleep operation is done.
    template <typename Rep, typename Period>
    inline task<void> sleep_for(std::chrono::duration<Rep, Period> duration, std::stop_token token = get_stop_token())
    {
        if (duration <= std::chrono::steady_clock::duration::zero())
            co_return;

        struct sleep_awaiter
        {
            std::chrono::steady_clock::duration duration;
            std::stop_token token;
            int result;
            std::exception_ptr exception;

            bool await_ready() const noexcept { return false; }
            void await_suspend(std::coroutine_handle<> h) noexcept
            {
                try
                {
                    detail::post_sleep_event([h]
                                             { h.resume(); }, duration, token, &result);
                }
                catch (...)
                {
                    exception = std::current_exception();
                    h.resume(); // Resume the coroutine even if an exception occurs
                }
            }
            void await_resume() noexcept
            {
                if (exception)
                {
                    std::rethrow_exception(exception);
                }
            }
        };

        co_await sleep_awaiter{std::chrono::duration_cast<std::chrono::steady_clock::duration>(duration), token};
    }

    inline task<void> shutdown()
    {
        detail::shutdown_runtime();
        return yield();
    }

#ifdef WEBCRAFT_ASYNC_RUNTIME_MOCK
    namespace detail
    {
        static void initialize_runtime() noexcept
        {
        }

        static void shutdown_runtime() noexcept
        {
            // Perform any necessary cleanup for the runtime
        }

        static void post_yield_event(std::function<void()> func, int *)
        {
            // Post the yield event to the runtime
            func();
        }

        static void post_sleep_event(std::function<void()> func, std::chrono::steady_clock::duration duration, std::stop_token token, int *)
        {
            // Post the sleep event to the runtime
            if (token.stop_requested())
                return;

            // Simulate a sleep operation
            std::this_thread::sleep_for(duration);
            func();
        }
    }
#endif

}