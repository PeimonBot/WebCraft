#pragma once

#include <concepts>
#include <exception>
#include <coroutine>
#include "event_signal.hpp"
#include <iostream>

namespace webcraft::async
{
    template <typename T>
    concept has_member_operator_co_await_v = requires(T &&t) {
        { t.operator co_await() };
    };

    template <typename T>
    concept is_awaitable_suspend_type = std::same_as<T, void> || std::convertible_to<T, bool> || std::convertible_to<T, std::coroutine_handle<>>;

    template <typename T>
    concept has_awaitable_elements = requires(T &&a, std::coroutine_handle<> h) {
        { a.await_ready() } -> std::convertible_to<bool>;
        { a.await_suspend(h) } -> is_awaitable_suspend_type;
        { a.await_resume() };
    };

    template <typename T>
    concept has_non_member_operator_co_await_v = requires(T &&t) {
        { operator co_await(t) };
    };

    template <typename T>
    concept awaitable_t = has_member_operator_co_await_v<T> || has_awaitable_elements<T> || has_non_member_operator_co_await_v<T>;

    namespace detail
    {
        template <typename T>
            requires(!awaitable_t<T>)
        struct basic_awaiter
        {
            T &t;

            constexpr basic_awaiter(T &t) noexcept : t(t) {}

            constexpr bool await_ready() const noexcept
            {
                return false;
            }

            constexpr void await_suspend(std::coroutine_handle<> h) const noexcept
            {
            }

            constexpr T await_resume() const
            {
                return std::forward<T>(t);
            }
        };

        template <typename T>
        auto get_awaiter(T &&t)
        {
            if constexpr (has_member_operator_co_await_v<T>)
            {
                return t.operator co_await();
            }
            else if constexpr (has_non_member_operator_co_await_v<T>)
            {
                return operator co_await(std::forward<T>(t));
            }
            else if constexpr (has_awaitable_elements<T>)
            {
                return std::move(t);
            }
            else
            {
                return basic_awaiter(t);
            }
        }
    }

    template <awaitable_t T>
    using awaitable_resume_t = decltype(detail::get_awaiter(std::declval<T>()).await_resume());

    // Forward declare
    template <typename T>
    class task;

    template <typename T>
    class task_promise
    {
    public:
        std::optional<T> value;
        std::exception_ptr exception;
        std::coroutine_handle<> continuation;

        task<T> get_return_object();

        std::suspend_never initial_suspend() noexcept { return {}; }

        struct final_awaiter
        {
            bool await_ready() noexcept { return false; }
            void await_suspend(std::coroutine_handle<task_promise> h) noexcept
            {
                if (h.promise().continuation)
                    h.promise().continuation.resume();
            }
            void await_resume() noexcept {}
        };
        final_awaiter final_suspend() noexcept { return {}; }

        void return_value(T v) noexcept(std::is_nothrow_move_constructible_v<T>)
        {
            value = std::move(v);
        }

        void unhandled_exception() noexcept
        {
            exception = std::current_exception();
        }
    };

    template <>
    class task_promise<void>
    {
    public:
        std::exception_ptr exception;
        std::coroutine_handle<> continuation;

        task<void> get_return_object();

        std::suspend_never initial_suspend() noexcept { return {}; }

        struct final_awaiter
        {
            bool await_ready() noexcept { return false; }
            void await_suspend(std::coroutine_handle<task_promise> h) noexcept
            {
                if (h.promise().continuation)
                    h.promise().continuation.resume();
            }
            void await_resume() noexcept {}
        };
        final_awaiter final_suspend() noexcept { return {}; }

        void return_void() noexcept {}

        void unhandled_exception() noexcept
        {
            exception = std::current_exception();
        }
    };

    template <typename T = void>
    class task
    {
    public:
        using promise_type = task_promise<T>;
        using handle_type = std::coroutine_handle<promise_type>;

        explicit task(handle_type h) noexcept : coro(h) {}

        task(task &&other) noexcept : coro(std::exchange(other.coro, {})) {}
        task &operator=(task &&other) noexcept
        {
            if (this != &other)
            {
                if (coro)
                    coro.destroy();
                coro = std::exchange(other.coro, {});
            }
            return *this;
        }

        ~task()
        {
            if (coro)
                coro.destroy();
        }

        bool await_ready() const noexcept
        {
            return !coro || coro.done();
        }

        void await_suspend(std::coroutine_handle<> h) noexcept
        {
            coro.promise().continuation = h;
        }

        T await_resume()
        {
            if (coro.promise().exception)
                std::rethrow_exception(coro.promise().exception);
            return coro.promise().value.value();
        }

    private:
        friend class task_promise<T>;
        handle_type coro;
    };

    template <>
    class task<void>
    {
    public:
        using promise_type = task_promise<void>;
        using handle_type = std::coroutine_handle<promise_type>;

        explicit task(handle_type h) noexcept : coro(h) {}

        task(task &&other) noexcept : coro(std::exchange(other.coro, {})) {}
        task &operator=(task &&other) noexcept
        {
            if (this != &other)
            {
                if (coro)
                    coro.destroy();
                coro = std::exchange(other.coro, {});
            }
            return *this;
        }

        ~task()
        {
            if (coro)
                coro.destroy();
        }

        bool await_ready() const noexcept
        {
            return !coro || coro.done();
        }

        void await_suspend(std::coroutine_handle<> h) noexcept
        {
            coro.promise().continuation = h;
        }

        void await_resume()
        {
            if (coro.promise().exception)
                std::rethrow_exception(coro.promise().exception);
        }

    private:
        friend class task_promise<void>;
        handle_type coro;
    };

    // Define get_return_object after task is complete
    template <typename T>
    task<T> task_promise<T>::get_return_object()
    {
        return task<T>{std::coroutine_handle<task_promise>::from_promise(*this)};
    }

    inline task<void> task_promise<void>::get_return_object()
    {
        return task<void>{std::coroutine_handle<task_promise>::from_promise(*this)};
    }

    template <awaitable_t T>
    awaitable_resume_t<T> sync_wait(T &&awaitable)
    {
        event_signal signal;
        std::exception_ptr exception;

        if constexpr (std::is_void_v<awaitable_resume_t<T>>)
        {

            auto async_fn = [&] -> task<void>
            {
                try
                {
                    co_await awaitable;
                }
                catch (...)
                {
                    exception = std::current_exception();
                }
                signal.set();
            };

            auto task = async_fn();

            signal.wait();
            if (exception)
            {
                std::rethrow_exception(exception);
            }
        }
        else
        {
            std::optional<awaitable_resume_t<T>> result;

            auto async_fn = [&] -> task<void>
            {
                try
                {
                    auto value = co_await awaitable;
                    result = std::move(value); // Store the result
                }
                catch (...)
                {
                    exception = std::current_exception();
                }
                signal.set();
            };
            auto task = async_fn();
            signal.wait();
            if (exception)
            {
                std::rethrow_exception(exception);
            }
            return result.value();
        }
    }
}