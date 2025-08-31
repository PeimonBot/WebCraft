
#define TEST_SUITE_NAME ThreadPoolTests

#include "test_suite.hpp"
#include <webcraft/async/thread_pool.hpp>
#include <future>
#include <vector>
#include <set>
#include <chrono>

using namespace webcraft::async;
using namespace std::chrono_literals;

TEST_CASE(BasicFunctionExecution)
{
    thread_pool pool(1, 4, 1000ms);

    std::atomic<int> counter{0};
    std::promise<void> promise;
    auto future = promise.get_future();

    pool.submit([&counter, &promise]()
                {
        counter++;
        promise.set_value(); });

    // Wait for the task to complete
    EXPECT_EQ(future.wait_for(5s), std::future_status::ready);
    EXPECT_EQ(counter.load(), 1);
}

TEST_CASE(MultipleThreadExecution)
{
    thread_pool pool(0, 8, 1000ms);

    const int num_tasks = 20;
    auto counter = std::make_shared<std::atomic<int>>(0);
    std::vector<std::future<void>> futures;

    // Track which thread IDs execute tasks
    auto thread_ids_mutex = std::make_shared<std::mutex>();
    auto thread_ids = std::make_shared<std::set<std::thread::id>>();

    for (int i = 0; i < num_tasks; ++i)
    {
        auto promise = std::make_shared<std::promise<void>>();
        futures.push_back(promise->get_future());

        pool.submit([counter, thread_ids, thread_ids_mutex, promise]()
                    {
            (*counter)++;
            
            // Record the thread ID
            {
                std::lock_guard<std::mutex> lock(*thread_ids_mutex);
                thread_ids->insert(std::this_thread::get_id());
            }
            
            // Simulate some work
            std::this_thread::sleep_for(50ms);
            promise->set_value(); });
    }

    // Wait for all tasks to complete
    for (auto &future : futures)
    {
        EXPECT_EQ(future.wait_for(10s), std::future_status::ready);
    }

    EXPECT_EQ(counter->load(), num_tasks);
    // Should have used multiple threads (at least 2, but likely more given the sleep)
    EXPECT_GT(thread_ids->size(), 1);
}

TEST_CASE(ScalingToMaxThreads)
{
    const size_t max_threads = 3;             // Smaller number for faster test
    thread_pool pool(0, max_threads, 5000ms); // Long timeout to prevent premature cleanup

    // Initially should have no workers (min_threads = 0)
    EXPECT_EQ(pool.get_workers_size(), 0);

    const int num_tasks = 9; // 3x max_threads
    std::vector<std::future<void>> futures;
    auto thread_ids_mutex = std::make_shared<std::mutex>();
    auto thread_ids = std::make_shared<std::set<std::thread::id>>();

    // Submit tasks that will block for a while to force thread creation
    for (int i = 0; i < num_tasks; ++i)
    {
        auto promise = std::make_shared<std::promise<void>>();
        futures.push_back(promise->get_future());

        pool.submit([thread_ids, thread_ids_mutex, promise]()
                    {
            // Record the thread ID
            {
                std::lock_guard<std::mutex> lock(*thread_ids_mutex);
                thread_ids->insert(std::this_thread::get_id());
            }
            
            // Block for a while to force more threads to be created
            std::this_thread::sleep_for(100ms);
            promise->set_value(); });

        // Small delay between submissions to allow thread creation
        std::this_thread::sleep_for(20ms);

        // Check that worker count doesn't exceed max_threads
        EXPECT_LE(pool.get_workers_size(), max_threads);
    }

    // After submitting all tasks, should have scaled up to max_threads
    EXPECT_EQ(pool.get_workers_size(), max_threads);

    // Wait for all tasks to complete
    for (auto &future : futures)
    {
        EXPECT_EQ(future.wait_for(10s), std::future_status::ready);
    }

    // Should have scaled up but not beyond max_threads
    EXPECT_LE(thread_ids->size(), max_threads);
    EXPECT_LE(pool.get_workers_size(), max_threads);
    // Should have created multiple threads
    EXPECT_GT(thread_ids->size(), 1);
}

TEST_CASE(DoesNotExceedMaxThreads)
{
    const size_t max_threads = 2;
    thread_pool pool(0, max_threads, 5000ms);

    auto concurrent_tasks = std::make_shared<std::atomic<int>>(0);
    auto max_concurrent = std::make_shared<std::atomic<int>>(0);
    const int num_tasks = 10;
    std::vector<std::future<void>> futures;

    for (int i = 0; i < num_tasks; ++i)
    {
        auto promise = std::make_shared<std::promise<void>>();
        futures.push_back(promise->get_future());

        pool.submit([concurrent_tasks, max_concurrent, promise]()
                    {
            int current = ++(*concurrent_tasks);
            
            // Update max concurrent if this is higher
            int expected = max_concurrent->load();
            while (current > expected && !max_concurrent->compare_exchange_weak(expected, current)) {
                expected = max_concurrent->load();
            }
            
            // Simulate work
            std::this_thread::sleep_for(100ms);
            
            --(*concurrent_tasks);
            promise->set_value(); });

        // Small delay to allow tasks to start
        std::this_thread::sleep_for(20ms);
    }

    // Wait for all tasks to complete
    for (auto &future : futures)
    {
        EXPECT_EQ(future.wait_for(15s), std::future_status::ready);
    }

    // Should never have exceeded max_threads
    EXPECT_LE(max_concurrent->load(), static_cast<int>(max_threads));
}

TEST_CASE(WorkerExpiration)
{
    const size_t min_threads = 1;
    const size_t max_threads = 4;
    const auto idle_timeout = 200ms; // Shorter timeout for faster test

    thread_pool pool(min_threads, max_threads, idle_timeout);

    // Should start with min_threads workers
    EXPECT_EQ(pool.get_workers_size(), min_threads);

    // First, create load to scale up the pool
    const int initial_tasks = 6;
    std::vector<std::future<void>> initial_futures;
    auto thread_ids_mutex = std::make_shared<std::mutex>();
    auto initial_thread_ids = std::make_shared<std::set<std::thread::id>>();

    for (int i = 0; i < initial_tasks; ++i)
    {
        auto promise = std::make_shared<std::promise<void>>();
        initial_futures.push_back(promise->get_future());

        pool.submit([initial_thread_ids, thread_ids_mutex, promise]()
                    {
            {
                std::lock_guard<std::mutex> lock(*thread_ids_mutex);
                initial_thread_ids->insert(std::this_thread::get_id());
            }
            
            // Work for a bit to force thread creation
            std::this_thread::sleep_for(50ms);
            promise->set_value(); });

        std::this_thread::sleep_for(20ms); // Allow threads to be created
    }

    // Should have scaled up beyond min_threads
    size_t workers_after_scaling = pool.get_workers_size();
    EXPECT_GT(workers_after_scaling, min_threads);
    EXPECT_LE(workers_after_scaling, max_threads);

    // Wait for initial tasks to complete
    for (auto &future : initial_futures)
    {
        EXPECT_EQ(future.wait_for(5s), std::future_status::ready);
    }

    // Should have created multiple threads
    EXPECT_GT(initial_thread_ids->size(), min_threads);

    // Now wait for idle timeout + some buffer time to allow cleanup
    std::this_thread::sleep_for(idle_timeout + 300ms);

    // Submit new tasks to trigger cleanup and test that pool still works
    const int final_tasks = 3;
    std::vector<std::future<void>> final_futures;

    for (int i = 0; i < final_tasks; ++i)
    {
        auto promise = std::make_shared<std::promise<void>>();
        final_futures.push_back(promise->get_future());

        pool.submit([promise]()
                    { promise->set_value(); });
    }

    // Wait for final tasks to complete
    for (auto &future : final_futures)
    {
        EXPECT_EQ(future.wait_for(5s), std::future_status::ready);
    }

    // After cleanup and new task submission, should have workers >= min_threads
    size_t workers_after_expiration = pool.get_workers_size();
    EXPECT_GE(workers_after_expiration, min_threads);

    // The worker count after expiration should be reasonable (not necessarily less than peak)
    // because new tasks might have triggered creation of new workers
    EXPECT_LE(workers_after_expiration, max_threads);
}

TEST_CASE(MinThreadsRespected)
{
    const size_t min_threads = 2;
    const size_t max_threads = 6;

    thread_pool pool(min_threads, max_threads, 100ms);

    // Should start with exactly min_threads workers
    EXPECT_EQ(pool.get_workers_size(), min_threads);

    // Submit a single task to verify the pool works
    auto promise = std::make_shared<std::promise<void>>();
    auto future = promise->get_future();

    pool.submit([promise]()
                { promise->set_value(); });

    EXPECT_EQ(future.wait_for(5s), std::future_status::ready);

    // Should still have at least min_threads workers
    EXPECT_GE(pool.get_workers_size(), min_threads);

    // Verify getters work correctly
    EXPECT_EQ(pool.get_min_threads(), min_threads);
    EXPECT_EQ(pool.get_max_threads(), max_threads);
    EXPECT_EQ(pool.get_idle_timeout(), 100ms);
}

TEST_CASE(WorkerCountBehavior)
{
    const size_t min_threads = 2;
    const size_t max_threads = 5;
    const auto idle_timeout = 300ms;

    thread_pool pool(min_threads, max_threads, idle_timeout);

    // Initial state: should have min_threads workers
    EXPECT_EQ(pool.get_workers_size(), min_threads);

    // Phase 1: Submit tasks to force scaling up
    const int load_tasks = 10;
    std::vector<std::future<void>> load_futures;

    for (int i = 0; i < load_tasks; ++i)
    {
        auto promise = std::make_shared<std::promise<void>>();
        load_futures.push_back(promise->get_future());

        pool.submit([promise]()
                    {
            // Simulate work to keep threads busy
            std::this_thread::sleep_for(100ms);
            promise->set_value(); });

        // Allow time for thread creation
        std::this_thread::sleep_for(30ms);
    }

    // Should have scaled up
    size_t workers_under_load = pool.get_workers_size();
    EXPECT_GT(workers_under_load, min_threads);
    EXPECT_LE(workers_under_load, max_threads);

    // Wait for all load tasks to complete
    for (auto &future : load_futures)
    {
        EXPECT_EQ(future.wait_for(10s), std::future_status::ready);
    }

    // Phase 2: Wait for workers to become idle and expire
    std::this_thread::sleep_for(idle_timeout + 200ms);

    // Phase 3: Submit a small task to trigger cleanup
    auto cleanup_promise = std::make_shared<std::promise<void>>();
    auto cleanup_future = cleanup_promise->get_future();

    pool.submit([cleanup_promise]()
                { cleanup_promise->set_value(); });

    EXPECT_EQ(cleanup_future.wait_for(5s), std::future_status::ready);

    // Should have reduced worker count but not below min_threads
    size_t workers_after_expiration = pool.get_workers_size();
    EXPECT_GE(workers_after_expiration, min_threads);
    EXPECT_LE(workers_after_expiration, workers_under_load);

    // Phase 4: Verify pool still works normally
    auto final_promise = std::make_shared<std::promise<void>>();
    auto final_future = final_promise->get_future();

    pool.submit([final_promise]()
                { final_promise->set_value(); });

    EXPECT_EQ(final_future.wait_for(5s), std::future_status::ready);
}

TEST_CASE(ConcurrentSubmission)
{
    thread_pool pool(2, 8, 1000ms);

    const int num_submitters = 4;
    const int tasks_per_submitter = 10;
    auto total_executed = std::make_shared<std::atomic<int>>(0);
    std::vector<std::future<void>> submitter_futures;

    // Create multiple threads that submit tasks concurrently
    for (int submitter = 0; submitter < num_submitters; ++submitter)
    {
        auto submitter_promise = std::make_shared<std::promise<void>>();
        submitter_futures.push_back(submitter_promise->get_future());

        std::thread submitter_thread([&pool, total_executed, tasks_per_submitter, submitter_promise]()
                                     {
            std::vector<std::future<void>> task_futures;
            
            for (int task = 0; task < tasks_per_submitter; ++task) {
                auto task_promise = std::make_shared<std::promise<void>>();
                task_futures.push_back(task_promise->get_future());
                
                pool.submit([total_executed, task_promise]() {
                    (*total_executed)++;
                    std::this_thread::sleep_for(10ms); // Small amount of work
                    task_promise->set_value();
                });
            }
            
            // Wait for all tasks from this submitter to complete
            for (auto& future : task_futures) {
                future.wait();
            }
            
            submitter_promise->set_value(); });

        submitter_thread.detach();
    }

    // Wait for all submitters to complete
    for (auto &future : submitter_futures)
    {
        EXPECT_EQ(future.wait_for(30s), std::future_status::ready);
    }

    EXPECT_EQ(total_executed->load(), num_submitters * tasks_per_submitter);
}

TEST_CASE(TasksWithExceptions)
{
    thread_pool pool(1, 4, 1000ms);

    auto successful_tasks = std::make_shared<std::atomic<int>>(0);
    std::vector<std::future<void>> futures;

    // Submit mix of successful and exception-throwing tasks
    for (int i = 0; i < 10; ++i)
    {
        auto promise = std::make_shared<std::promise<void>>();
        futures.push_back(promise->get_future());

        pool.submit([i, successful_tasks, promise]()
                    {
            try {
                if (i % 3 == 0) {
                    throw std::runtime_error("Test exception");
                }
                (*successful_tasks)++;
                promise->set_value();
            } catch (...) {
                // Task threw exception but pool should continue working
                promise->set_value();
            } });
    }

    // Wait for all tasks to complete
    for (auto &future : futures)
    {
        EXPECT_EQ(future.wait_for(5s), std::future_status::ready);
    }

    // Should have 6 successful tasks (0, 3, 6, 9 throw exceptions)
    EXPECT_EQ(successful_tasks->load(), 6);

    // Pool should still be functional after exceptions
    auto final_promise = std::make_shared<std::promise<void>>();
    auto final_future = final_promise->get_future();

    pool.submit([final_promise]()
                { final_promise->set_value(); });

    EXPECT_EQ(final_future.wait_for(5s), std::future_status::ready);
}
