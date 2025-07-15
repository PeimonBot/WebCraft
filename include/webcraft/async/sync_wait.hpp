#pragma once

#include "task.hpp"

namespace webcraft::async
{

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