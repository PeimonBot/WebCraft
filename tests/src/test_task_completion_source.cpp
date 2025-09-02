///////////////////////////////////////////////////////////////////////////////
// Copyright (c) Aditya Rao
// Licenced under MIT license. See LICENSE.txt for details.
///////////////////////////////////////////////////////////////////////////////


#define TEST_SUITE_NAME TaskCompletionSourceTestSuite

#include "test_suite.hpp"
#include <string>
#include <webcraft/async/async.hpp>
#include <webcraft/async/task_completion_source.hpp>

using namespace webcraft::async;
using namespace std::chrono_literals;

// Static assertions to ensure task_completion_source works with different types
static_assert(!std::is_move_constructible_v<task_completion_source<int>>, "task_completion_source<int> should be move constructible");
static_assert(!std::is_move_assignable_v<task_completion_source<int>>, "task_completion_source<int> should be move assignable");
static_assert(!std::is_copy_constructible_v<task_completion_source<int>>, "task_completion_source<int> should not be copy constructible");
static_assert(!std::is_copy_assignable_v<task_completion_source<int>>, "task_completion_source<int> should not be copy assignable");

static_assert(!std::is_move_constructible_v<task_completion_source<void>>, "task_completion_source<void> should be move constructible");
static_assert(!std::is_move_assignable_v<task_completion_source<void>>, "task_completion_source<void> should be move assignable");
static_assert(!std::is_copy_constructible_v<task_completion_source<void>>, "task_completion_source<void> should not be copy constructible");
static_assert(!std::is_copy_assignable_v<task_completion_source<void>>, "task_completion_source<void> should not be copy assignable");

TEST_CASE(TestTaskCompletionSourceBasicUsageInt)
{
    task_completion_source<int> tcs;
    auto t = tcs.task();

    // Set the value
    int value = 42;
    tcs.set_value(std::move(value));
    std::cout << "Value was set" << std::endl;

    // The task should complete with the value
    auto result = sync_wait(t);
    std::cout << "Value was received: " << result << std::endl;
    EXPECT_EQ(result, 42) << "Task should complete with the value 42";
    std::cout << "Checking value" << std::endl;
}

TEST_CASE(TestTaskCompletionSourceBasicUsageString)
{
    task_completion_source<std::string> tcs;
    auto t = tcs.task();

    // Set the value
    std::string value = "Hello, World!";
    tcs.set_value(std::move(value));

    // The task should complete with the value
    auto result = sync_wait(t);
    std::cout << "Value was set: " << result << std::endl;
    EXPECT_EQ(result, "Hello, World!") << "Task should complete with the string value";
}

TEST_CASE(TestTaskCompletionSourceBasicUsageVoid)
{
    task_completion_source<void> tcs;
    auto t = tcs.task();

    event_signal signal;

    // Create a task that waits for completion and sets a signal
    auto waiter = [&]() -> task<void>
    {
        co_await t;
        signal.set();
    };

    auto waiter_task = waiter();

    // Set the completion
    tcs.set_value();

    // Wait for the waiter task to complete
    sync_wait(waiter_task);
    EXPECT_TRUE(signal.is_set()) << "Task should complete and signal should be set";
}

TEST_CASE(TestTaskCompletionSourceException)
{
    task_completion_source<int> tcs;
    auto t = tcs.task();

    // Set an exception
    try
    {
        throw std::runtime_error("Test exception");
    }
    catch (...)
    {
        tcs.set_exception(std::current_exception());
    }

    // The task should throw the exception
    EXPECT_THROW(sync_wait(t), std::runtime_error) << "Task should throw the set exception";
}

TEST_CASE(TestTaskCompletionSourceExceptionVoid)
{
    task_completion_source<void> tcs;
    auto t = tcs.task();

    // Set an exception
    try
    {
        throw std::logic_error("Test exception");
    }
    catch (...)
    {
        tcs.set_exception(std::current_exception());
    }

    // The task should throw the exception
    EXPECT_THROW(sync_wait(t), std::logic_error) << "Void task should throw the set exception";
}

TEST_CASE(TestTaskCompletionSourceAsynchronousCompletion)
{
    task_completion_source<std::string> tcs;
    auto t = tcs.task();

    // Start a thread that will set the value after a delay
    std::thread([&]()
                {
        std::this_thread::sleep_for(test_timer_timeout);
        std::string value = "Async result";
        tcs.set_value(std::move(value)); })
        .detach();

    auto start = std::chrono::steady_clock::now();
    auto result = sync_wait(t);
    auto end = std::chrono::steady_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    EXPECT_GE(duration, test_timer_timeout) << "Task should wait for asynchronous completion";
    EXPECT_EQ(result, "Async result") << "Task should return the asynchronously set value";
}

TEST_CASE(TestTaskCompletionSourceAsynchronousException)
{
    task_completion_source<int> tcs;
    auto t = tcs.task();

    // Start a thread that will set an exception after a delay
    std::thread([&]()
                {
        std::this_thread::sleep_for(test_timer_timeout);
        try {
            throw std::invalid_argument("Async exception");
        } catch (...) {
            tcs.set_exception(std::current_exception());
        } })
        .detach();

    auto start = std::chrono::steady_clock::now();

    EXPECT_THROW(sync_wait(t), std::invalid_argument) << "Task should throw asynchronously set exception";

    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    EXPECT_GE(duration, test_timer_timeout) << "Task should wait for asynchronous exception";
}

TEST_CASE(TestTaskCompletionSourceWithComplexType)
{
    struct ComplexType
    {
        int id;
        std::string name;
        std::vector<double> values;

        bool operator==(const ComplexType &other) const
        {
            return id == other.id && name == other.name && values == other.values;
        }
    };

    task_completion_source<ComplexType> tcs;
    auto t = tcs.task();

    ComplexType complex_value{42, "test", {1.1, 2.2, 3.3}};
    tcs.set_value(std::move(complex_value));

    auto result = sync_wait(t);
    EXPECT_EQ(result.id, 42) << "Complex type id should be preserved";
    EXPECT_EQ(result.name, "test") << "Complex type name should be preserved";
    EXPECT_EQ(result.values.size(), 3) << "Complex type vector size should be preserved";
    EXPECT_DOUBLE_EQ(result.values[0], 1.1) << "Complex type vector values should be preserved";
}

TEST_CASE(TestTaskCompletionSourceValueType)
{
    task_completion_source<int> tcs_int;
    static_assert(std::same_as<typename decltype(tcs_int)::value_type, int>, "value_type should be int");

    task_completion_source<std::string> tcs_string;
    static_assert(std::same_as<typename decltype(tcs_string)::value_type, std::string>, "value_type should be std::string");

    task_completion_source<void> tcs_void;
    static_assert(std::same_as<typename decltype(tcs_void)::value_type, void>, "value_type should be void");
}

TEST_CASE(TestTaskCompletionSourceTaskReturnsCorrectType)
{
    task_completion_source<int> tcs_int;
    auto task_int = tcs_int.task();
    static_assert(std::same_as<decltype(task_int), task<int>>, "task() should return task<int>");

    task_completion_source<std::string> tcs_string;
    auto task_string = tcs_string.task();
    static_assert(std::same_as<decltype(task_string), task<std::string>>, "task() should return task<std::string>");

    task_completion_source<void> tcs_void;
    auto task_void = tcs_void.task();
    static_assert(std::same_as<decltype(task_void), task<void>>, "task() should return task<void>");
}

TEST_CASE(TestTaskCompletionSourceCancellationScenario)
{
    task_completion_source<int> tcs;
    auto t = tcs.task();

    bool task_completed = false;
    std::exception_ptr caught_exception;

    auto waiter = [&]() -> task<void>
    {
        try
        {
            auto result = co_await t;
            task_completed = true;
        }
        catch (...)
        {
            caught_exception = std::current_exception();
        }
    };

    auto waiter_task = waiter();

    // Simulate cancellation by setting an exception
    try
    {
        throw std::runtime_error("Operation cancelled");
    }
    catch (...)
    {
        tcs.set_exception(std::current_exception());
    }

    sync_wait(waiter_task);

    EXPECT_FALSE(task_completed) << "Task should not complete successfully when cancelled";
    EXPECT_TRUE(caught_exception != nullptr) << "Exception should be caught";

    try
    {
        std::rethrow_exception(caught_exception);
    }
    catch (const std::runtime_error &e)
    {
        EXPECT_STREQ(e.what(), "Operation cancelled") << "Exception message should match";
    }
}

TEST_CASE(TestTaskCompletionSourceThroughput)
{
    constexpr int iterations = 10000;
    std::atomic<int> completed_count{0};

    auto run = [&]() -> task<void>
    {
        for (int i = 0; i < iterations; ++i)
        {
            task_completion_source<int> tcs;
            auto task = tcs.task();

            // Complete immediately
            int value = i;
            tcs.set_value(std::move(value));

            auto result = co_await task;
            EXPECT_EQ(result, i);
            completed_count++;
        }
    };

    sync_wait(run());
    EXPECT_EQ(completed_count.load(), iterations) << "All task completion sources should complete successfully";
}
