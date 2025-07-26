#pragma once

#include <coroutine>
#include <type_traits>
#include <utility>
#include <exception>
#include <vector>
#include "task.hpp"

namespace webcraft::async
{
    template <typename T>
    class task_completion_source final
    {
    private:
        void *m_state;                                  // Placeholder for the actual state type
        std::vector<std::coroutine_handle<>> m_waiters; // List of waiters
        std::exception_ptr m_exception;                 // Exception pointer if an error occurs

    public:
        using value_type = T;

        task_completion_source() : m_state(nullptr) {}
        task_completion_source(const task_completion_source &) = delete;
        task_completion_source &operator=(const task_completion_source &) = delete;
        task_completion_source(task_completion_source &&other) : m_state(std::exchange(other.m_state, nullptr)), m_waiters(std::exchange(other.m_waiters, {})), m_exception(std::exchange(other.m_exception, nullptr))
        {
        }
        task_completion_source &operator=(task_completion_source &&other) noexcept
        {
            if (this != &other)
            {
                m_state = std::exchange(other.m_state, nullptr);
                m_waiters = std::exchange(other.m_waiters, {});
                m_exception = std::exchange(other.m_exception, nullptr);
            }
            return *this;
        }

        ~task_completion_source() = default;

        // Method to set the value
        void set_value(T &&value)
            requires(!std::is_void_v<T> && (std::is_copy_constructible_v<T> || std::is_move_constructible_v<T>))
        {
            // Implementation to set the value in the state
            m_state = std::addressof(value); // Placeholder for actual value storage
            for (auto &waiter : m_waiters)
            {
                if (!waiter.done())
                {
                    waiter.resume(); // Resume the coroutine waiting for the value
                }
            }
            m_waiters.clear();
        }

        void set_value()
            requires std::is_void_v<T>
        {
            // Implementation for void type
            for (auto &waiter : m_waiters)
            {
                if (!waiter.done())
                {
                    waiter.resume(); // Resume the coroutine waiting for the value
                }
            }
            m_waiters.clear();
        }

        // Method to set an exception
        void set_exception(std::exception_ptr e)
        {
            m_exception = std::move(e);
            for (auto &waiter : m_waiters)
            {
                if (!waiter.done())
                {
                    waiter.resume();
                }
            }
            m_waiters.clear();
        }

        webcraft::async::task<T> task()
        {
            struct awaiter
            {
                task_completion_source &source;

                bool await_ready() const noexcept
                {
                    return !source.m_state; // Check if the state is ready
                }

                void await_suspend(std::coroutine_handle<> h) noexcept
                {
                    source.m_waiters.push_back(h); // Add the coroutine handle to waiters
                }

                T await_resume()
                {
                    if (source.m_exception)
                    {
                        std::rethrow_exception(source.m_exception); // Rethrow the stored exception
                    }

                    if constexpr (!std::is_void_v<T>)
                    {
                        return *static_cast<T *>(source.m_state); // Return the stored value
                    }
                }
            };

            if constexpr (std::is_void_v<T>)
            {
                co_await awaiter{*this};
            }
            else
            {
                co_return co_await awaiter{*this};
            }
        }
    };
};