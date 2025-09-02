#pragma once

///////////////////////////////////////////////////////////////////////////////
// Copyright (c) Aditya Rao
// Licenced under MIT license. See LICENSE.txt for details.
///////////////////////////////////////////////////////////////////////////////


#include <functional>
#include <chrono>
#include <stop_token>
#include <memory>
#include <concepts>
#include <string>

#include <webcraft/async/task.hpp>
#include <webcraft/async/sync_wait.hpp>
#include <webcraft/async/fire_and_forget_task.hpp>

namespace webcraft::async
{

    namespace detail
    {
        void initialize_runtime() noexcept;

        void shutdown_runtime() noexcept;

        class runtime_callback
        {
        public:
            /// @brief Tries to execute the callback and gives result to consumer
            /// @param result the result of runtime event
            /// @param cancelled the cancellation status
            virtual void try_execute(int result, bool cancelled = false) = 0;
        };

        class runtime_event : public runtime_callback
        {
        private:
            std::function<void()> callback;
            int result;
            std::atomic<bool> finished{false};
            bool cancelled{false};
            std::stop_token token;
            std::unique_ptr<std::stop_callback<std::function<void()>>> stop_callback;

        protected:
            /// @brief Tries to natively start the async operation
            virtual void try_start() = 0;

            /// @brief Try to natively cancel the async operation
            virtual void try_native_cancel() = 0;

        public:
            runtime_event(std::stop_token token) : token(token)
            {
            }

            virtual ~runtime_event() = default;

            /// @brief Tries to execute the callback and gives result to consumer
            /// @param result the result of runtime event
            /// @param cancelled the cancellation status
            void try_execute(int result, bool cancelled = false) override
            {
                bool expected = false;
                if (finished.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                {
                    this->cancelled = cancelled;
                    this->result = result;
                    callback();
                }
            }

            /// @brief Starts the asynchronous operation
            /// @param callback the callback to be executed once operation is complete
            void start_async(std::function<void()> callback)
            {
                this->callback = std::move(callback);

                runtime_event *ev = this;

                stop_callback = std::make_unique<std::stop_callback<std::function<void()>>>(
                    token,
                    [ev]
                    {
                        ev->try_native_cancel();
                        ev->try_execute(-1, true); // Indicate cancellation
                    });

                try_start();
            }

            bool is_cancelled() const
            {
                return cancelled;
            }

            int get_result() const
            {
                return result;
            }
        };

        std::unique_ptr<runtime_event> post_yield_event();

        std::unique_ptr<runtime_event> post_sleep_event(std::chrono::steady_clock::duration duration, std::stop_token token);

        template <typename T>
        concept awaitable_event_t = awaitable_t<T> && requires(T &&t) {
            { t.get_result() } -> std::convertible_to<int>;
            { t.is_cancelled() } -> std::convertible_to<bool>;
        };

        template <typename SmartPtrType>
        struct runtime_event_awaiter
        {
            SmartPtrType event;
            bool cancelled;
            std::exception_ptr ptr{nullptr};

            bool await_ready() const noexcept { return false; }
            void await_suspend(std::coroutine_handle<> h)
            {
                try
                {
                    event->start_async([h]
                                       { h.resume(); });
                }
                catch (...)
                {
                    ptr = std::current_exception();
                    h.resume();
                }
            }
            void await_resume()
            {
                if (ptr)
                {
                    std::rethrow_exception(ptr);
                }
            }

            int get_result()
            {
                return event->get_result();
            }

            bool is_cancelled()
            {
                return event->is_cancelled();
            }
        };

        template <typename T>
            requires std::is_base_of_v<runtime_event, T>
        awaitable_event_t auto as_awaitable(std::unique_ptr<T> &event)
        {
            return runtime_event_awaiter{event};
        }

        template <typename T>
            requires std::is_base_of_v<runtime_event, T>
        awaitable_event_t auto as_awaitable(std::unique_ptr<T> &&event)
        {
            return runtime_event_awaiter{std::move(event)};
        }

        uint64_t get_native_handle();

#ifdef __linux__
        std::mutex &get_runtime_mutex();
#elif defined(__APPLE__)
        int16_t get_kqueue_filter();
        uint32_t get_kqueue_flags();
#endif
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
        co_await detail::as_awaitable(detail::post_yield_event());
    }

    /// @brief Sleeps for a specified duration, allowing other tasks to run during the sleep.
    /// @param duration The duration to sleep.
    /// @param token The stop token to check for cancellation requests.
    /// @return A task that completes when the sleep operation is done.
    template <typename Rep, typename Period>
    inline task<void> sleep_for(std::chrono::duration<Rep, Period> duration, std::stop_token token = get_stop_token())
    {
        if (duration <= std::chrono::steady_clock::duration::zero() || token.stop_requested())
            co_return;

        co_await detail::as_awaitable(detail::post_sleep_event(duration, token));
    }

    template <typename Rep, typename Period>
    inline fire_and_forget_task set_timeout(std::function<void()> func, std::chrono::duration<Rep, Period> duration, std::stop_token token = get_stop_token())
    {
        co_await sleep_for(duration, token);
        if (token.stop_requested())
            co_return;
        func();
    }

    template <typename Rep, typename Period>
    inline fire_and_forget_task set_interval(std::function<void()> func, std::chrono::duration<Rep, Period> duration, std::stop_token token = get_stop_token())
    {
        while (!token.stop_requested())
        {
            co_await sleep_for(duration, token);
            if (token.stop_requested())
                co_return;
            func();
        }
    }

    inline task<void> shutdown()
    {
        detail::shutdown_runtime();
        return yield();
    }

}