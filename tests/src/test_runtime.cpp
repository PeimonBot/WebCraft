#define TEST_SUITE_NAME RuntimeTestSuite

// #define WEBCRAFT_ASYNC_RUNTIME_MOCK

#include "test_suite.hpp"
#include <webcraft/async/async.hpp>

using namespace webcraft::async;
using namespace std::chrono_literals;

TEST_CASE(TestRuntimeInitAndDestroy)
{
    runtime_context context;
}

TEST_CASE(TestSimpleRuntime)
{
    std::cout << "Starting TestSimpleRuntime..." << std::endl;
    runtime_context context;

    auto task_fn = []() -> task<void>
    {
        int count = 0;
        for (int i = 0; i < 10; ++i)
        {
            int temp = count;
            co_await yield();
            EXPECT_EQ(temp, count) << "Count should remain the same during yield";
            count++;
        }
        EXPECT_EQ(count, 10) << "Count should be 10 after yielding 10 times";
    };

    sync_wait(task_fn());

    std::cout << "TestSimpleRuntime completed successfully." << std::endl;
}

TEST_CASE(TestRuntimeTimerTask)
{
    std::cout << "Starting TestRuntimeTimerTask..." << std::endl;
    runtime_context context;

    auto timer_task = []() -> task<void>
    {
        for (int i = 0; i < 3; i++)
        {
            auto timer_duration = test_timer_timeout * (i + 1);
            auto start_time = std::chrono::steady_clock::now();
            co_await sleep_for(timer_duration);
            auto end_time = std::chrono::steady_clock::now();

            auto elapsed_time = end_time - start_time;
            EXPECT_GE(elapsed_time, timer_duration - test_adjustment_factor) << "Elapsed time should be greater than or equal to timer duration";
        }
    };

    sync_wait(timer_task());

    std::cout << "TestRuntimeTimerTask completed successfully." << std::endl;
}

TEST_CASE(TestRuntimeTimerCancellationTask)
{
    std::cout << "Starting TestRuntimeTimerCancellationTask..." << std::endl;
    runtime_context context;

    auto timer_task = []() -> task<void>
    {
        std::stop_source source;

        std::thread([&source]
                    {
            std::this_thread::sleep_for(test_cancel_timeout);
            source.request_stop(); })
            .detach();

        std::stop_token token = source.get_token();

        auto start_time = std::chrono::steady_clock::now();

        co_await sleep_for(test_timer_timeout, token);
        auto end_time = std::chrono::steady_clock::now();

        auto elapsed_time = end_time - start_time;
        EXPECT_GE(elapsed_time, test_cancel_timeout) << "The timer should have been cancelled after " << test_cancel_timeout.count() << "ms";
    };

    sync_wait(timer_task());

    std::cout << "TestRuntimeTimerCancellationTask completed successfully." << std::endl;
}