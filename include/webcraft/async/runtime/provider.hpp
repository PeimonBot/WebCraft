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

namespace webcraft::async::runtime::detail
{
    /// @brief the native runtime event on the producer side, this will be whats done in the coroutine awaitables for example:
    ///
    /// ```
    /// class basic_awaitable {
    ///     std::unique_ptr<event> ev;
    ///     constexpr bool await_ready() { return false; }
    ///     void await_suspend(std::coroutine_handle<> h) {
    ///         ev = std::make_unique<event>([h] { h.resume(); }); // This is where the event is constructed
    ///         ev->start_async(); // Here is where io_uring_submit, Windows Overlapped events, and kqueue events will occur
    ///     }
    ///     void await_resume() {
    ///         int result = ev->get_result();
    ///         ev.reset();
    ///     }
    /// };
    /// ```
    /// On the waiter side this will look like:
    /// ```
    /// // result will be set in the function
    /// auto* event_ptr = wait_and_get_event();
    /// if (event_ptr) {
    ///     event_ptr->resume(); // This is where the event is executed, for example, in io_uring this will submit the request
    /// }
    /// ```
    class native_runtime_event
    {
    private:
        std::function<void()> callback;
        int result = 0;
        std::atomic<bool> fired{false};
        std::atomic<bool> canceled{false};

    public:
        /// @brief constructs the native runtime event, this is where the event details are created/initialized.
        /// For io_uring, this means getting the cqe and prepping the request.
        /// For Windows, this means creating the Overlapped structure and setting the callback.
        /// For kqueue, this means creating the kevent structure and setting the callback (and if its a nop, firing the user listener).
        /// @param callback the callback to be called when the event is executed
        native_runtime_event(std::function<void()> callback) : callback(std::move(callback)) {};
        /// @brief Destructor for the native runtime event, this is where the event is cleaned up.
        /// For io_uring, this means freeing the cqe.
        /// For Windows, this means freeing the Overlapped structure.
        /// For kqueue, this means freeing the kevent structure and deleting the event from the queue.
        virtual ~native_runtime_event() = default;
        /// @brief Starts the asynchronous operation, this is where the event is submitted to the runtime.
        /// For io_uring, this means submitting the request to the io_uring queue.
        /// For Windows, this means posting the Overlapped structure to the IOCP.
        /// For kqueue, this means submitting the kevent to the kqueue.
        virtual void start_async() = 0;
        /// @brief Gets the result of the async operation, this is where the result of the event is retrieved.
        /// @return the result
        constexpr int get_result() const
        {
            return result;
        }

        /// @brief Sets the result of the async operation, this is where the result of the event is set. Should be called on the consumption side after getting event success status.
        /// @param res the result to set
        virtual void set_result(int res)
        {
            result = res;
            if (!canceled.load())
            {
                fired.store(true);
            }
        }

        /// @brief Resumes the event, this is where the event is executed. Should be called on the consumption side after getting event success status.
        void execute_callback()
        {
            if (callback)
            {
                callback();
            }
        }

        /// @brief Cancels the event, this is where the event is canceled. Can be called anywhere.
        virtual void cancel()
        {
            canceled.store(true);
        }

        /// @brief Checks if event has fired, this is used to check if the event has been executed.
        /// @return has fired
        [[nodiscard]] bool has_fired() const
        {
            return fired.load();
        }

        /// @brief Checks if event has been canceled, this is used to check if the event has been canceled.
        /// @return has been canceled
        [[nodiscard]] bool is_canceled() const
        {
            return canceled.load();
        }
    };

    /// @brief Provider for the runtime
    class runtime_provider
    {
    private:
        ::async::event_signal shutdown_signal;

    public:
        runtime_provider() = default;

        virtual ~runtime_provider() = default;

        virtual void setup_runtime() = 0;

        virtual void cleanup_runtime() = 0;

        /// @brief Creates a NOP event, this is used for yielding control back to the runtime without doing anything.
        /// @return the NOP event
        virtual std::unique_ptr<native_runtime_event> create_nop_event(std::function<void()> callback) = 0;

        /// @brief Creates a timer event that will be executed after the duration specified.
        /// @param duration the duration to wait before executing the event
        /// @return the timer event
        virtual std::unique_ptr<native_runtime_event> create_timer_event(std::chrono::steady_clock::duration duration, std::function<void()> callback) = 0;

        /// @brief Waits for an event to be ready and returns it.
        /// @return the event that is ready, or nullptr if no event is ready
        virtual native_runtime_event *wait_and_get_event() = 0;

        /// @brief Runs the IO loop, this is where the runtime will process events and execute them.
        void run_io_loop()
        {
            while (!shutdown_signal.is_set())
            {
                auto event = wait_and_get_event();
                if (event)
                {
                    event->execute_callback();
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
            runner rn{shutdown_signal, value, callable, exception_ptr};

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
        ::async::task<void> yield()
        {

            struct yield_awaiter
            {
                runtime_provider *provider;
                std::unique_ptr<native_runtime_event> event;

                yield_awaiter(runtime_provider *provider) : provider(provider)
                {
                }

                constexpr bool await_ready() const noexcept
                {
                    return false; // Always yield control back to the runtime
                }

                bool await_suspend(std::coroutine_handle<> h)
                {
                    auto handle = h;
                    event = provider->create_nop_event(
                        [handle]() mutable
                        {
                            if (handle && !handle.done())
                            {
                                handle.resume(); // Resume the coroutine when the event is executed
                            }
                        });

                    if (!event)
                    {
                        throw std::runtime_error("Failed to create NOP event in yield awaiter");
                    }
                    event->start_async(); // Start the async operation
                    return true;
                }

                void await_resume()
                {
                    // No result to resume, just yield control
                    event.reset();
                }
            };

            co_await yield_awaiter(this);
        }

        /// @brief Sleeps for duration (unless canceled then it will resume)
        /// @param duration the duration to sleep for
        /// @param token the token to cancel the sleep
        /// @return the awaitable
        virtual ::async::task<void> sleep_for(std::chrono::steady_clock::duration duration, std::stop_token token = {})
        {

            struct timer_awaiter
            {
                runtime_provider *provider;
                std::unique_ptr<std::stop_callback<std::function<void()>>> cancelation_callback;
                std::unique_ptr<native_runtime_event> event;
                std::optional<std::coroutine_handle<>> handle;

                timer_awaiter(runtime_provider *provider, std::stop_token token, std::chrono::steady_clock::duration dur) : provider(provider)
                {
                    event = provider->create_timer_event(dur,
                                                         [this]
                                                         {
                                                             if (handle && !handle.value().done())
                                                             {
                                                                 handle.value().resume(); // Resume the coroutine when the event is executed
                                                             }
                                                         });
                    cancelation_callback = std::make_unique<std::stop_callback<std::function<void()>>>(
                        token, [this]()
                        {
                                if (!event->has_fired() && !event->is_canceled())
                                {
                                    event->cancel(); // Cancel the timer event
                                } });
                }

                constexpr bool await_ready() const noexcept
                {
                    return false; // Always yield control back to the runtime
                }

                void await_suspend(std::coroutine_handle<> h)
                {
                    handle = h;
                    event->start_async(); // Start the async operation
                }

                void await_resume() noexcept
                {
                    cancelation_callback.reset();
                    event.reset();
                }
            };

            co_await timer_awaiter(this, token, duration);
        }

        /// @brief Shuts down the runtime
        /// @return the awaitable
        ::async::task<void> shutdown()
        {
            shutdown_signal.set();
            return yield();
        }
    };

    std::shared_ptr<runtime_provider> get_runtime_provider();

    // Keep this for later when we have multiple providers like libuv and libevent and the existing native providers.
    enum class provider_type
    {
        NATIVE
    };

    constexpr auto runtime_provider_type = provider_type::NATIVE;
}