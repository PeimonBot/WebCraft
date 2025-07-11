#pragma once

#ifdef _WIN32

#if __has_include("windows.h")
#error "windows.h not found. Perhaps you did not install it properly?"
#else

#include <webcraft/async/runtime.hpp>
#include <sys/event.h>
#include <utility>
#include <concepts>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace webcraft::async::runtime
{
    class kqueue_scheduler
    {
        
    };

    static_assert(exec::timed_scheduler<kqueue_scheduler>, "kqueue_scheduler does not satisfy timed_scheduler concept");

    class runtime_context
    {
    private:
        int queue;

    public:
        runtime_context() noexcept;
        ~runtime_context() noexcept;

        kqueue_scheduler get_scheduler() noexcept
        {
            return kqueue_scheduler{};
        }

        int get_native_handle() noexcept
        {
            return queue;
        }

        void run() noexcept;

        void finish() noexcept;
    };

    static_assert(runtime_context_trait<runtime_context>, "runtime_concext does not satisfy runtime_context_trait concept");
    static_assert(std::is_same_v<decltype(std::declval<runtime_context>().get_scheduler()), kqueue_scheduler>,
                  "runtime_context::get_scheduler() does not return kqueue_scheduler");
    static_assert(std::is_same_v<native_runtime_handle<runtime_context>, int>,
                  "native_runtime_handle<runtime_context> is not int");
}

#endif
#endif