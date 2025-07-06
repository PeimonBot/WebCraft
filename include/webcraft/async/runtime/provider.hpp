#pragma once

#include <memory>
#include <functional>
#include <atomic>
#include <coroutine>
#include <chrono>
#include <async/task.h>
#include <async/awaitable_resume_t.h>
#include <async/event_signal.h>
#include <webcraft/async/awaitable.hpp>
#include <concepts>
#include <exception>

#include <iostream>

namespace webcraft::async::runtime::detail
{
    class runtime_event
    {
        // private:
        //     std::atomic<bool> resumed{false};
        //     int result;

        // protected:
        //     std::coroutine_handle<> handle;

    public:
        runtime_event()
        {
        }

        virtual ~runtime_event() = default;

        // virtual bool await_resume() const noexcept
        // {
        //     return true;
        // }

        // constexpr bool await_ready() const noexcept
        // {
        //     return false; // Always yield control back to the runtime
        // }

        // virtual void await_suspend(std::coroutine_handle<> h)
        // {
        //     this->handle = h;
        //     try_start();
        // }

        virtual void try_resume() = 0;
        // void try_resume()
        // {
        //     // throw std::runtime_error("await_suspend not implemented in base class");
        //     if (!resumed.exchange(true) && !handle.done())
        //     {
        //         std::cout << "Result: " << result << ", Resumed: " << resumed.load() << std::endl;
        //         std::cerr << "Resuming coroutine: " << handle.address() << std::endl;
        //         handle.resume(); // Resume the coroutine when the event is executed
        //         std::cerr << "Resumed coroutine: " << handle.address() << std::endl;
        //     }
        // }

        // virtual void try_start() noexcept = 0;

        // void set_result(int res) noexcept
        // {
        //     result = res;
        // }

        // int get_result() const
        // {
        //     return result;
        // }
    };

    // class cancellable_runtime_event : public runtime_event, public std::enable_shared_from_this<cancellable_runtime_event>
    // {
    // private:
    //     std::atomic<bool> canceled{false};
    //     std::stop_token token;
    //     std::unique_ptr<std::stop_callback<std::function<void()>>> cancelation_callback;

    // public:
    //     cancellable_runtime_event(std::stop_token token) : token(token)
    //     {
    //     }

    //     virtual ~cancellable_runtime_event() = default;

    //     bool await_resume() const noexcept override
    //     {
    //         return !canceled.load(); // Resume only if not canceled
    //     }

    //     void await_suspend(std::coroutine_handle<> h) override
    //     {
    //         runtime_event::await_suspend(h);

    //         auto self = shared_from_this(); // Keep this object alive

    //         cancelation_callback = std::make_unique<std::stop_callback<std::function<void()>>>(token, [self]()
    //                                                                                            {
    //             if (!self->canceled.exchange(true))
    //             {
    //                 self->try_native_cancel(); // Cancel the native operation
    //                 self->try_resume(); // Resume the coroutine if it was waiting
    //             } });
    //     }

    //     virtual void try_native_cancel() noexcept = 0;
    // };

    /// @brief Provider for the runtime
    class runtime_provider
    {
    private:
        ::async::event_signal shutdown_signal;

    public:
        runtime_provider() = default;

        virtual ~runtime_provider() = default;

        /// @brief Waits for an event to be ready and returns it.
        /// @return the event that is ready, or nullptr if no event is ready
        virtual runtime_event *wait_and_get_event() = 0;

        /// @brief Runs the IO loop, this is where the runtime will process events and execute them.
        void run_io_loop()
        {
            while (!shutdown_signal.is_set())
            {
                auto event = wait_and_get_event();
                if (event)
                {
                    event->try_resume();
                }
            }
        }

        /// @brief Runs an asynchronous function
        /// @param callable the asynchronous function
        /// @return the return type of the awaitable returned by the invocable
        auto run(std::invocable auto callable)
            requires webcraft::async::awaitable<std::invoke_result_t<decltype(callable)>>
        {
            using CallableType = decltype(callable);
            using ReturnType = ::async::awaitable_resume_t<std::invoke_result_t<CallableType>>;
            std::shared_ptr<ReturnType> value;
            std::exception_ptr exception_ptr = nullptr;

            // Create the runner that will execute the callable and set the value
            struct runner
            {
                runtime_provider *provider;
                ::async::event_signal &shutdown_signal;
                std::shared_ptr<ReturnType> ptr;
                CallableType &callable;
                std::exception_ptr &exception_ptr;

                // The run function that will be executed in the coroutine
                ::async::task<void> run()
                {
                    try
                    {
                        if constexpr (std::is_void_v<ReturnType>)
                        {
                            co_await callable();
                        }
                        else
                        {
                            *ptr = co_await callable();
                        }
                    }
                    catch (...)
                    {
                        exception_ptr = std::current_exception(); // Capture the exception
                    }
                    shutdown_signal.set();
                }
            };

            // Run the callable in a coroutine
            runner rn{this, shutdown_signal, value, callable, exception_ptr};

            auto task = rn.run();

            // Run the IO loop to process events and execute the task
            run_io_loop();

            if (exception_ptr)
            {
                std::rethrow_exception(exception_ptr); // If an exception was thrown, rethrow it
            }

            // Wait for the task to complete
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
        // virtual ::async::task<bool> sleep_for(std::chrono::steady_clock::duration duration, std::stop_token token = {}) = 0;

        /// @brief Shuts down the runtime
        /// @return the awaitable
        ::async::task<void> shutdown()
        {
            shutdown_signal.set();
            return yield();
        }
    };

    std::shared_ptr<runtime_provider> get_runtime_provider();
}