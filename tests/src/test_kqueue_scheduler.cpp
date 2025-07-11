#ifdef __APPLE__

#define TEST_SUITE_NAME KqueueSchedulerTestSuite
#include "test_suite.hpp"
#include <webcraft/async/runtime.macos.hpp>
#include <chrono>
#include <thread>
#include <atomic>

using namespace webcraft::async::runtime;
using namespace std::chrono_literals;

TEST_CASE(kqueue_scheduler_basic_schedule)
{
    runtime_context context;
    
    std::jthread worker([&](std::stop_token st) {
        std::stop_callback cb{st, [&]{ context.finish(); }};
        context.run();
    });
    
    std::atomic<bool> called = false;
    
    auto scheduler = context.get_scheduler();
    auto sender = scheduler.schedule();
    
    auto operation = stdexec::connect(
        std::move(sender),
        stdexec::then([&called]() {
            called = true;
        })
    );
    
    stdexec::start(operation);
    
    // Wait for the operation to complete
    std::this_thread::sleep_for(100ms);
    
    EXPECT_TRUE(called) << "Callback was not called";
}

TEST_CASE(kqueue_scheduler_schedule_after)
{
    runtime_context context;
    
    std::jthread worker([&](std::stop_token st) {
        std::stop_callback cb{st, [&]{ context.finish(); }};
        context.run();
    });
    
    std::atomic<bool> called = false;
    
    auto scheduler = context.get_scheduler();
    auto start_time = std::chrono::steady_clock::now();
    auto duration = 500ms;
    
    auto sender = exec::schedule_after(scheduler, duration);
    
    auto operation = stdexec::connect(
        std::move(sender),
        stdexec::then([&called]() {
            called = true;
        })
    );
    
    stdexec::start(operation);
    
    // Wait for the operation to complete
    std::this_thread::sleep_for(1s);
    
    auto end_time = std::chrono::steady_clock::now();
    auto elapsed = end_time - start_time;
    
    EXPECT_TRUE(called) << "Callback was not called";
    EXPECT_GE(elapsed, duration) << "Timer fired too early";
}

TEST_CASE(kqueue_scheduler_schedule_at)
{
    runtime_context context;
    
    std::jthread worker([&](std::stop_token st) {
        std::stop_callback cb{st, [&]{ context.finish(); }};
        context.run();
    });
    
    std::atomic<bool> called = false;
    
    auto scheduler = context.get_scheduler();
    auto start_time = std::chrono::steady_clock::now();
    auto target_time = start_time + 500ms;
    
    auto sender = exec::schedule_at(scheduler, target_time);
    
    auto operation = stdexec::connect(
        std::move(sender),
        stdexec::then([&called]() {
            called = true;
        })
    );
    
    stdexec::start(operation);
    
    // Wait for the operation to complete
    std::this_thread::sleep_for(1s);
    
    auto end_time = std::chrono::steady_clock::now();
    
    EXPECT_TRUE(called) << "Callback was not called";
    EXPECT_GE(end_time, target_time) << "Timer fired too early";
}

TEST_CASE(kqueue_scheduler_multiple_timers)
{
    runtime_context context;
    
    std::jthread worker([&](std::stop_token st) {
        std::stop_callback cb{st, [&]{ context.finish(); }};
        context.run();
    });
    
    constexpr int num_timers = 5;
    std::atomic<int> completed_count = 0;
    
    auto scheduler = context.get_scheduler();
    
    for (int i = 0; i < num_timers; ++i) {
        auto duration = std::chrono::milliseconds(100 * (i + 1));
        
        auto sender = exec::schedule_after(scheduler, duration);
        
        auto operation = stdexec::connect(
            std::move(sender),
            stdexec::then([&completed_count]() {
                completed_count++;
            })
        );
        
        stdexec::start(operation);
    }
    
    // Wait for all timers to complete
    std::this_thread::sleep_for(1s);
    
    EXPECT_EQ(completed_count, num_timers) << "Not all timers completed";
}

#endif

// Made with Bob
