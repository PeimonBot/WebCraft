#pragma once

#ifdef __linux__

#ifndef __has_include("liburing.h")
#error "liburing.h not found. Perhaps you did not install it properly?"
#else

#include <webcraft/async/runtime.hpp>
#include <liburing.h>
#include <utility>
#include <concepts>

namespace webcraft::async::runtime
{
    class io_uring_scheduler
    {
        
    };

    static_assert(exec::timed_scheduler<io_uring_scheduler>, "io_uring_scheduler does not satisfy timed_scheduler concept");

    class runtime_context
    {
    private:
        io_uring ring;

    public:
        runtime_context() noexcept;
        ~runtime_context() noexcept;

        io_uring_scheduler get_scheduler() noexcept
        {
            return io_uring_scheduler{};
        }

        io_uring *get_native_handle() noexcept
        {
            return &ring;
        }

        void run() noexcept;

        void finish() noexcept;
    };

    static_assert(runtime_context_trait<runtime_context>, "runtime_concext does not satisfy runtime_context_trait concept");
    static_assert(std::is_same_v<decltype(std::declval<runtime_context>().get_scheduler()), io_uring_scheduler>,
                  "runtime_context::get_scheduler() does not return io_uring_scheduler");
    static_assert(std::is_same_v<native_runtime_handle<runtime_context>, io_uring *>,
                  "native_runtime_handle<runtime_context> is not io_uring*");
}

#endif