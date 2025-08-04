#pragma once

#include <coroutine>
#include <vector>

namespace webcraft::async
{

    struct async_event
    {
    private:
        std::vector<std::coroutine_handle<>> handles;
        std::atomic<bool> flag{false};

    public:
        bool await_ready() { return is_set(); }
        constexpr void await_suspend(std::coroutine_handle<> h)
        {
            this->handles.push_back(h);
        }
        constexpr void await_resume() {}

        void set()
        {
            if (flag.exchange(true, std::memory_order_release))
                return;
            // resumes the coroutine
            for (auto &h : this->handles)
            {
                if (!h.done())
                {
                    h.resume();
                }
            }
            this->handles.clear();
        }

        bool is_set() const
        {
            return flag.load(std::memory_order_acquire);
        }
    };
}