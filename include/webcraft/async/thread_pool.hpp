#pragma once

#include <thread>
#include <chrono>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <stop_token>
#include <unordered_map>
#include <queue>

using namespace std::chrono_literals;

namespace webcraft::async
{

    class thread_pool;

    class thread_pool_worker
    {
    private:
        std::jthread thr;
        thread_pool *pool;

    public:
        thread_pool_worker(thread_pool *pool) : pool(pool) {}
        ~thread_pool_worker()
        {
            request_stop();
        }

        void request_stop()
        {
            thr.request_stop();
        }

        void run_loop(std::stop_token token);

        std::thread::id get_id()
        {
            return thr.get_id();
        }
    };

    class thread_pool
    {
    private:
        const size_t min_threads, max_threads;
        const std::chrono::milliseconds idle_timeout;
        std::unordered_map<std::thread::id, std::unique_ptr<thread_pool_worker>> workers;
        std::queue<std::function<void()>> tasks;
        std::mutex mutex;
        std::condition_variable cv;
        std::atomic<int> available_workers{0};
        friend class thread_pool_worker;

    public:
        thread_pool(size_t min_threads = 0, size_t max_threads = std::thread::hardware_concurrency(), std::chrono::milliseconds idle_timeout = 10'000ms) : min_threads(min_threads), max_threads(max_threads), idle_timeout(idle_timeout)
        {
            for (int i = 0; i < min_threads; i++)
            {
                auto worker = std::make_unique<thread_pool_worker>(this);
                workers[worker->get_id()] = std::move(worker);
            }
        }

        ~thread_pool()
        {
            for (auto &it : workers)
            {
                it.second->request_stop();
            }

            cv.notify_all();
            workers.clear();
        }

        void submit(std::function<void()> func)
        {
            {
                std::unique_lock<std::mutex> lock(mutex);
                if (available_workers == 0 && workers.size() < max_threads)
                {
                    auto worker = std::make_unique<thread_pool_worker>(this);
                    workers[worker->get_id()] = std::move(worker);
                }
                tasks.push(func);
            }
            cv.notify_one();
        }

        const size_t get_min_threads() const
        {
            return min_threads;
        }

        const size_t get_max_threads() const
        {
            return max_threads;
        }

        const std::chrono::milliseconds get_idle_timeout() const
        {
            return idle_timeout;
        }
    };

    void thread_pool_worker::run_loop(std::stop_token token)
    {
        while (!token.stop_requested())
        {
            std::function<void()> task;

            (pool->available_workers)++;
            {
                std::unique_lock<std::mutex> lock(pool->mutex);

                auto status = pool->cv.wait_for(lock, pool->get_idle_timeout());

                if (status == std::cv_status::timeout)
                {
                    if (pool->available_workers > pool->get_max_threads())
                    {
                        (pool->available_workers)--;
                        pool->workers.erase(get_id());
                        return;
                    }
                    else if (token.stop_requested() && pool->tasks.empty())
                    {
                        return;
                    }
                    else
                    {
                        continue;
                    }
                }
                else
                {
                    if (token.stop_requested() && pool->tasks.empty())
                    {
                        return;
                    }
                }

                task = std::move(pool->tasks.front());
                pool->tasks.pop();
            }
            (pool->available_workers)--;
            task();
        }
    }
}