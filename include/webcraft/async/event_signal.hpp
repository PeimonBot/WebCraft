#pragma once

///////////////////////////////////////////////////////////////////////////////
// Copyright (c) Aditya Rao
// Licenced under MIT license. See LICENSE.txt for details.
///////////////////////////////////////////////////////////////////////////////


#include <atomic>
#include <chrono>
#include <thread>

namespace webcraft::async
{

    class immovable
    {
    public:
        immovable() = default;
        immovable(const immovable &) = delete;
        immovable &operator=(const immovable &) = delete;
        immovable(immovable &&) = delete;
        immovable &operator=(immovable &&) = delete;

        ~immovable() = default;
    };

    class event_signal : public immovable
    {
    private:
        std::atomic<bool> flag;

    public:
        event_signal() : flag(false) {}

        void set() noexcept
        {
            flag.store(true, std::memory_order_release);
        }

        void reset() noexcept
        {
            flag.store(false, std::memory_order_release);
        }

        bool is_set() const noexcept
        {
            return flag.load(std::memory_order_acquire);
        }

        bool wait_for(std::chrono::milliseconds timeout) const
        {
            auto start = std::chrono::steady_clock::now();
            while (!is_set())
            {
                if (std::chrono::steady_clock::now() - start >= timeout)
                {
                    return false; // Timeout
                }
                std::this_thread::yield(); // Yield to avoid busy waiting
            }
            return true; // Signal was set
        }

        bool wait() const
        {
            while (!is_set())
            {
                std::this_thread::yield(); // Yield to avoid busy waiting
            }
            return true; // Signal was set
        }

        bool operator()() const
        {
            return is_set();
        }

        explicit operator bool() const
        {
            return is_set();
        }
    };
}