#pragma once

#include <coroutine>
#include <vector>

namespace webcraft::async
{

    struct async_event
    {
        std::vector<std::coroutine_handle<>> handles;

        constexpr bool await_ready() { return false; }
        constexpr void await_suspend(std::coroutine_handle<> h)
        {
            this->handles.push_back(h);
        }
        constexpr void await_resume() {}

        void set()
        {
            // resumes the coroutine
            for (auto &h : this->handles)
            {
                if (!h.done())
                {
                    h.resume();
                }
            }
        }
    };
}