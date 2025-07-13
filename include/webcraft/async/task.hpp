#pragma once

#include <concepts>
#include <exception>
#include <coroutine>
#include "awaitable.hpp"
#include "event_signal.hpp"
#include <iostream>
#include <ranges>

namespace webcraft::async
{
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

    // then() implementation
    template <typename Func>
    struct then_awaitable
    {
        Func func;

        explicit then_awaitable(Func f) : func(std::move(f)) {}
    };

    template <typename Func>
    then_awaitable<Func> then(Func &&func)
    {
        return then_awaitable<Func>{std::forward<Func>(func)};
    }

    template <typename Awaitable, typename Func>
    auto operator|(Awaitable &&awaitable, then_awaitable<Func> then_op) -> task<std::invoke_result_t<Func, awaitable_resume_t<Awaitable>>>
        requires(!std::same_as<awaitable_resume_t<Awaitable>, void>)
    {
        using result_type = std::invoke_result_t<Func, awaitable_resume_t<Awaitable>>;

        auto result = co_await std::forward<Awaitable>(awaitable);

        if constexpr (awaitable_t<result_type>)
        {
            co_return co_await then_op.func(result); // If result is also awaitable, await it
        }
        else
        {
            co_return then_op.func(result);
        }
    }

    template <typename Awaitable, typename Func>
    auto operator|(Awaitable &&awaitable, then_awaitable<Func> then_op) -> task<std::invoke_result_t<Func>>
        requires std::same_as<awaitable_resume_t<Awaitable>, void>
    {
        using result_type = std::invoke_result_t<Func>;

        co_await std::forward<Awaitable>(awaitable);
        if constexpr (awaitable_t<result_type>)
        {
            co_return co_await then_op.func(); // If result is also awaitable, await it
        }
        else
        {
            co_return then_op.func();
        }
    }
}