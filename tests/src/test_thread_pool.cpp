
#define TEST_SUITE_NAME ThreadPoolTests

#include "test_suite.hpp"
#include <webcraft/async/thread_pool.hpp>
#include <future>
#include <vector>
#include <set>
#include <chrono>

using namespace webcraft::async;
using namespace std::chrono_literals;

TEST_CASE(RunOnDifferentThread)
{
    thread_pool thpr(2);

    auto id = std::this_thread::get_id();
    std::thread::id id2;
    event_signal sig;

    thpr.submit(
        [id, &id2, &sig]()
        {
            id2 = std::this_thread::get_id();
            sig.set();
        });

    sig.wait();
    EXPECT_NE(id, id2) << "The task should run on a different thread";
}

TEST_CASE(RunMultipleTasksOnMultipleThreads)
{
    constexpr size_t num_tasks = 4;
    thread_pool thpr(num_tasks, num_tasks);

    auto id = std::this_thread::get_id();

    std::atomic<int> success_counter{0}, run_counter{0};
    std::mutex mtx;
    std::condition_variable cv;

    for (size_t i = 0; i < num_tasks; ++i)
    {
        thpr.submit(
            [id, &success_counter, &run_counter, &cv]()
            {
                auto id2 = std::this_thread::get_id();
                if (id != id2)
                {
                    ++success_counter;
                    ++run_counter;
                    cv.notify_all();
                }
            });
    }

    {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait_for(lock, 2s, [&run_counter, num_tasks]()
                    { return run_counter.load() == num_tasks; });
    }
    EXPECT_EQ(success_counter.load(), num_tasks) << "All tasks should run on different threads";
}

TEST_CASE(RunMoreThanMinimumTasks)
{
}

TEST_CASE(RunMoreThanMaxTasks)
{
}

TEST_CASE(TestWorkerExpiry) {}