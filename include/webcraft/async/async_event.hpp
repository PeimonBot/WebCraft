#pragma once

///////////////////////////////////////////////////////////////////////////////
// Copyright (c) Aditya Rao
// Licenced under MIT license. See LICENSE.txt for details.
///////////////////////////////////////////////////////////////////////////////

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
            if (!flag.load())
            {
                this->handles.push_back(h);
            }
            else
            {
                h.resume();
            }
        }
        constexpr void await_resume() {}

        void set()
        {
            bool expected = false;
            if (flag.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                return;

            auto copy = handles;
            this->handles.clear();
            // resumes the coroutine
            for (auto &h : copy)
            {
                if (!h.done())
                {
                    h.resume();
                }
            }
        }

        bool is_set() const
        {
            return flag;
        }

        void reset()
        {
            flag = false;
        }
    };
}