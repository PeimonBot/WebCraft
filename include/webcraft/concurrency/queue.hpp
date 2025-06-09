#pragma once
#include <atomic>
#include <array>
#include <optional>
#include <string>

namespace webcraft::concurrency
{

    template <class T, size_t CAP>
    struct atomic_ring_buffer
    {
        std::array<T, CAP> buf_;
        std::atomic<std::size_t> head_{0}; // producer end (owner only)
        std::atomic<std::size_t> tail_{0}; // consumer end (shared)
    };

    template <class T, size_t ORDER>
    class lock_free_deque
    {
    private:
        atomic_ring_buffer<T, 1 << ORDER> buf_;
        constexpr static size_t cap = 1 << ORDER;

    public:
        lock_free_deque() = default;
        ~lock_free_deque() = default;
        lock_free_deque(const lock_free_deque &) = delete;
        lock_free_deque &operator=(const lock_free_deque &) = delete;
        lock_free_deque(lock_free_deque &&) = default;
        lock_free_deque &operator=(lock_free_deque &&) = default;

        bool push_front(T h)
        {
            auto head = buf_.head_.load(std::memory_order_relaxed);
            auto tail = buf_.tail_.load(std::memory_order_acquire);
            if (head - tail >= cap)
                return false; // full – drop / fallback
            buf_.buf_[head & (cap - 1)] = h;
            std::atomic_thread_fence(std::memory_order_release);
            buf_.head_.store(head + 1, std::memory_order_relaxed);
            return true;
        }

        bool push_back(T h)
        {
            auto head = buf_.head_.load(std::memory_order_acquire);
            auto tail = buf_.tail_.load(std::memory_order_relaxed);
            if (head - tail >= cap)
                return false; // full – drop / fallback
            buf_.buf_[tail & (cap - 1)] = h;
            std::atomic_thread_fence(std::memory_order_release);
            buf_.tail_.store(tail + 1, std::memory_order_relaxed);
            return true;
        }

        std::optional<T> pop_front()
        {
            auto head = buf_.head_.load(std::memory_order_acquire);
            std::atomic_thread_fence(std::memory_order_seq_cst);
            auto tail = buf_.tail_.load(std::memory_order_relaxed);
            if (head <= tail)
                return {}; // empty
            auto h = buf_.buf_[tail & (cap - 1)];
            if (!buf_.tail_.compare_exchange_strong(tail, tail + 1, std::memory_order_release, std::memory_order_relaxed))
            {
                return std::nullopt;
            }

            return h;
        }

        std::optional<T> pop_back()
        {
            auto tail = buf_.tail_.load(std::memory_order_acquire);
            std::atomic_thread_fence(std::memory_order_seq_cst);
            auto head = buf_.head_.load(std::memory_order_relaxed);
            if (tail >= head)
                return {}; // empty
            auto h = buf_.buf_[head - 1 & (cap - 1)];
            if (!buf_.head_.compare_exchange_strong(head, head - 1, std::memory_order_release, std::memory_order_relaxed))
            {
                return std::nullopt;
            }

            return h;
        }

        bool is_empty() const noexcept
        {
            return buf_.head_.load(std::memory_order_acquire) == buf_.tail_.load(std::memory_order_acquire);
        }
    };

}