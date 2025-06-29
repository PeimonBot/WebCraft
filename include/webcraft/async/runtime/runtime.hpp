#pragma once

#include <memory>
#include <concepts>
#include <coroutine>
#include <chrono>
#include <async/task.h>
#include <async/awaitable_resume_t.h>
#include <async/event_signal.h>
#include <async/task_completion_source.h>
#include <webcraft/async/awaitable.hpp>

namespace webcraft::async::runtime
{

    namespace detail
    {
        
    }

    /// @brief Provider for the runtime
    class RuntimeProvider
    {
    protected:
        ::async::event_signal shutdown_signal;

        virtual void run_io_loop() = 0;

    public:
        virtual ~RuntimeProvider() = default;

        /// @brief Runs an asynchronous function
        /// @param callable the asynchronous function
        /// @return the return type of the awaitable returned by the invocable
        auto run(std::invocable auto callable)
            requires webcraft::async::awaitable<std::invoke_result_t<decltype(callable)>>
        {
            using CallableType = decltype(callable);
            using ReturnType = ::async::awaitable_resume_t<std::invoke_result_t<CallableType>>;
            std::shared_ptr<ReturnType> value;

            struct runner
            {
                ::async::event_signal &shutdown_signal;
                std::shared_ptr<ReturnType> ptr;
                CallableType &callable;

                ::async::task<void> run()
                {
                    if constexpr (std::is_void_v<ReturnType>)
                    {
                        co_await callable();
                    }
                    else
                    {
                        *ptr = co_await callable();
                    }
                    shutdown_signal.set();
                }
            };

            runner rn{shutdown_signal, value, callable};
            auto task = rn.run();

            run_io_loop();

            if constexpr (!std::is_void_v<ReturnType>)
            {
                return *value;
            }
        }

        /// @brief Yields control back to the runtime
        /// @return the task
        virtual ::async::task<void> yield() = 0;

        /// @brief Sleeps for duration (unless canceled then it will resume)
        /// @param duration the duration to sleep for
        /// @param token the token to cancel the sleep
        /// @return the awaitable
        virtual ::async::task<void> sleep_for(std::chrono::steady_clock::duration duration, std::stop_token token = {}) = 0;

        /// @brief Shuts down the runtime
        /// @return the awaitable
        ::async::task<void> shutdown()
        {
            shutdown_signal.set();
            return yield();
        }
    };

    std::shared_ptr<RuntimeProvider> get_runtime_provider();

    /// @brief runs the asynchronous function (callable) synchronously
    /// @tparam ...Args the arguments for the callable object
    /// @param callable the callable object which returns an awaitable type
    /// @return the resumed type of the awaitable type returned by the callable object
    template <typename... Args>
    auto run(std::invocable<Args...> auto callable, Args &&...args)
        requires webcraft::async::awaitable<std::invoke_result_t<decltype(callable), Args...>>
    {
        using ReturnType = std::invoke_result_t<decltype(callable), Args...>;

        auto provider = get_runtime_provider();

        return provider->run([&]()
                             { return callable(args...); });
    }

    /// @brief yields control to the runtime. will be requeued and executed again.
    /// @return the awaitable for this function
    ::async::task<void> yield()
    {
        auto provider = get_runtime_provider();
        return provider->yield();
    }

    /// @brief sleeps for the duration specified (unless canceled then it will resume)
    /// @param duration the duration to be slept for
    /// @param token token used to cancel the timer and resume immedietly
    /// @return the awaitable for the function
    template <class Rep, class Duration>
    ::async::task<void> sleep_for(std::chrono::duration<Rep, Duration> duration, std::stop_token token = {})
    {
        auto provider = get_runtime_provider();
        return provider->sleep_for(std::chrono::duration_cast<std::chrono::steady_clock::duration>(duration), token);
    }

    /// @brief Sets the timeout for the duration specified and asynchronously invokes the function once timeout has expired (unless its canceled by the stop token)
    /// @tparam function the type of the function
    /// @param duration the duration to wait for
    /// @param fn the function to invoke after the duration has elapsed
    /// @param token the token to cancel the timeout
    template <std::invocable function, class Rep, class Duration>
    void set_timeout(std::chrono::duration<Rep, Duration> duration, function fn, std::stop_token token = {})
    {
        using ReturnType = std::invoke_result_t<function>;

        auto func = [duration, fn = std::move(fn), token]() -> ::async::task<void>
        {
            co_await sleep_for(duration, token);

            if (!token.stop_requested())
            {

                if constexpr (awaitable<ReturnType>)
                {
                    co_await fn();
                }
                else
                {
                    fn();
                }
            }
        };

        func();
    }

    /// @brief Invokes the asynchronous function every interval specified by the duration amount
    /// @tparam function the type of the function
    /// @param duration the duration of each interval
    /// @param fn the function to invoke every interval
    /// @param token the token to stop executing
    template <std::invocable function, class Rep, class Duration>
    void set_interval(std::chrono::duration<Rep, Duration> duration, function fn, std::stop_token token = {})
    {
        using ReturnType = std::invoke_result_t<function>;

        auto func = [duration, fn = std::move(fn), token]() -> ::async::task<void>
        {
            while (!token.stop_requested())
            {

                co_await sleep_for(duration, token);

                if (!token.stop_requested())
                {

                    if constexpr (awaitable<ReturnType>)
                    {
                        co_await fn();
                    }
                    else
                    {
                        fn();
                    }
                }
            }
        }

        func();
    }

    /// @brief shuts down the runtime
    /// @return the awaitable for this function
    ::async::task<void> shutdown()
    {
        auto provider = get_runtime_provider();
        return provider->shutdown();
    }
}