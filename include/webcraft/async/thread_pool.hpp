#pragma once

#include <thread>
#include <chrono>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <stop_token>
#include <unordered_map>
#include <queue>
#include <set>

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
        thread_pool_worker(thread_pool *pool) : pool(pool)
        {
            thr = std::jthread([this](std::stop_token token)
                               { run_loop(token); });
        }

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
        std::set<std::thread::id> workers_to_remove; // Track workers that should be removed
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

                // Clean up workers that have marked themselves for removal
                cleanup_expired_workers();

                if (available_workers == 0 && workers.size() < max_threads)
                {
                    auto worker = std::make_unique<thread_pool_worker>(this);
                    auto worker_id = worker->get_id();
                    workers[worker_id] = std::move(worker);
                }
                tasks.push(func);
            }
            cv.notify_one();
        }

    private:
        void cleanup_expired_workers()
        {
            // This method should be called while holding the mutex
            for (auto it = workers_to_remove.begin(); it != workers_to_remove.end();)
            {
                // Only remove workers if we have more than min_threads
                if (workers.size() <= min_threads)
                {
                    // Don't remove any more workers - we're at minimum
                    workers_to_remove.clear();
                    break;
                }

                auto worker_it = workers.find(*it);
                if (worker_it != workers.end())
                {
                    // Request stop and remove the worker
                    worker_it->second->request_stop();
                    workers.erase(worker_it);
                }
                it = workers_to_remove.erase(it);
            }
        }

    public:
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

        const size_t get_workers_size() const
        {
            return workers.size();
        }
    };

    inline void thread_pool_worker::run_loop(std::stop_token token)
    {
        while (!token.stop_requested())
        {
            std::function<void()> task;
            bool should_exit = false;

            (pool->available_workers)++;
            {
                std::unique_lock<std::mutex> lock(pool->mutex);

                auto status = pool->cv.wait_for(lock, pool->get_idle_timeout(), [this, &token]()
                                                { return !pool->tasks.empty() || token.stop_requested(); });

                if (!status && pool->tasks.empty())
                {
                    // Timeout occurred and no tasks available
                    if (pool->workers.size() > pool->get_min_threads())
                    {
                        // Mark this worker for removal instead of removing immediately
                        pool->workers_to_remove.insert(get_id());
                        should_exit = true;
                    }
                }

                if (token.stop_requested())
                {
                    (pool->available_workers)--;
                    return;
                }

                if (should_exit)
                {
                    (pool->available_workers)--;
                    return; // Exit the loop, worker will be cleaned up later
                }

                if (!pool->tasks.empty())
                {
                    task = std::move(pool->tasks.front());
                    pool->tasks.pop();
                }
                else
                {
                    (pool->available_workers)--;
                    continue;
                }
            }
            (pool->available_workers)--;
            task();
        }
    }
}