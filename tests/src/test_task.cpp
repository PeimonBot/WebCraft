

#define TEST_SUITE_NAME TaskTestSuite

#include "test_suite.hpp"
#include <string>
#include <webcraft/async/async.hpp>

using namespace webcraft::async;
using namespace std::chrono_literals;

// ensure that not_awaitable is not considered awaitable
struct not_awaitable
{
};

static_assert(!awaitable_t<not_awaitable>, "not_awaitable should not be considered awaitable");

// ensuring that awaitable types are correctly identified
struct awaitable_with_co_await
{
    auto operator co_await() const noexcept { return std::suspend_always{}; }
};

static_assert(awaitable_t<awaitable_with_co_await>, "awaitable_with_co_await should be considered awaitable");
static_assert(std::same_as<awaitable_resume_t<awaitable_with_co_await>, void>, "awaitable_with_co_await should resume to void");

struct awaitable_with_awaitable_elements
{
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<>) const noexcept {}
    void await_resume() const noexcept {}
};

static_assert(awaitable_t<awaitable_with_awaitable_elements>, "awaitable_with_awaitable_elements should be considered awaitable");
static_assert(std::same_as<awaitable_resume_t<awaitable_with_awaitable_elements>, void>, "awaitable_with_co_await should resume to void");

struct awaitable_outside
{
};

auto operator co_await(awaitable_outside) noexcept
{
    return std::suspend_always{};
}

static_assert(awaitable_t<awaitable_outside>, "awaitable_outside should be considered awaitable");
static_assert(std::same_as<awaitable_resume_t<awaitable_outside>, void>, "awaitable_with_co_await should resume to void");

// ensuring that awaitable with a resume type is correctly identified
struct awaitable_with_resume_type_int
{
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<>) const noexcept {}
    int await_resume() const noexcept { return 42; }
};

static_assert(awaitable_t<awaitable_with_resume_type_int>, "awaitable_with_resume_type_int should be considered awaitable");
static_assert(std::same_as<awaitable_resume_t<awaitable_with_resume_type_int>, int>, "awaitable_with_resume_type_int should resume to int");

// ensuring that task is awaitable
static_assert(awaitable_t<task<int>>, "task<int> should be considered awaitable");
static_assert(std::same_as<awaitable_resume_t<task<int>>, int>, "task<int> should resume to int");
static_assert(awaitable_t<task<not_awaitable>>, "task<int> should be considered awaitable");
static_assert(std::same_as<awaitable_resume_t<task<not_awaitable>>, not_awaitable>, "task<not_awaitable> should resume to not_awaitable");
static_assert(awaitable_t<task<void>>, "task<int> should be considered awaitable");
static_assert(std::same_as<awaitable_resume_t<task<void>>, void>, "task<void> should resume to void");

// ensuring that sync_wait works correctly with different awaitable types
static_assert(std::same_as<decltype(sync_wait(std::declval<task<void>>())), void>, "sync_wait should return void for awaitable_with_co_await");
static_assert(std::same_as<decltype(sync_wait(std::declval<task<int>>())), int>, "sync_wait should return int for awaitable_with_co_await");
static_assert(std::same_as<decltype(sync_wait(std::declval<task<not_awaitable>>())), not_awaitable>, "sync_wait should return not_awaitable for awaitable_with_co_await");
static_assert(std::same_as<decltype(sync_wait(std::declval<awaitable_with_co_await>())), void>, "sync_wait should return void for awaitable_with_co_await");
static_assert(std::same_as<decltype(sync_wait(std::declval<awaitable_with_resume_type_int>())), int>, "sync_wait should return int for awaitable_with_resume_type_int");

TEST_CASE(TestingWithSyncWaitVoid)
{
    event_signal signal;

    auto makeTask = [&]() -> task<>
    {
        signal.set();
        co_return;
    };

    auto task = makeTask();
    sync_wait(task);
    EXPECT_TRUE(signal.is_set()) << "sync_wait should set the signal";

    signal.reset();

    sync_wait(makeTask());
    EXPECT_TRUE(signal.is_set()) << "sync_wait should set the signal again";
}

TEST_CASE(TestingWithSyncWait)
{

    auto makeTask = []() -> task<std::string>
    {
        co_return "foo";
    };

    auto task = makeTask();
    EXPECT_EQ(sync_wait(task), "foo") << "sync_wait should return 'foo' from the task";

    EXPECT_EQ(sync_wait(makeTask()), "foo") << "sync_wait should return 'foo' from the task";
}

TEST_CASE(TestingSyncWaitWithAnotherThread)
{

    struct thread_awaitable
    {
        bool await_ready() const noexcept { return false; }
        void await_suspend(std::coroutine_handle<> h) const noexcept
        {
            std::thread([h]()
                        { h.resume(); })
                .detach();
        }
        void await_resume() const noexcept {}
    };

    auto asyncfn = []() -> task<void>
    {
        auto id = std::this_thread::get_id();
        co_await thread_awaitable();
        auto new_id = std::this_thread::get_id();
        EXPECT_NE(id, new_id) << "Task should resume on a different thread";
    };

    sync_wait(asyncfn());
}

TEST_CASE(TestTaskThroughput)
{
    auto completesSynchronously = []() -> task<int>
    {
        co_return 1;
    };

    auto run = [&]() -> task<>
    {
        int sum = 0;
        for (int i = 0; i < 1'000'000; ++i)
        {
            sum += co_await completesSynchronously();
        }
        EXPECT_EQ(sum, 1'000'000) << "Sum should be 1,000,000";
    };

    sync_wait(run());
}

TEST_CASE(TestTaskThrows)
{
    auto throws = []() -> task<int>
    {
        throw std::runtime_error("Test exception");
        co_return 42; // This line should never be reached
    };

    EXPECT_THROW(sync_wait(throws()), std::runtime_error) << "sync_wait should throw std::runtime_error";
}

TEST_CASE(TestTaskCompletesAsynchronously)
{
    async_event ev;

    auto async_fn = [&]() -> task<int>
    {
        co_await ev;
        co_return 42;
    };

    auto task = async_fn();

    std::thread([&]()
                {
        std::this_thread::sleep_for(test_timer_timeout);
        ev.set(); })
        .detach();

    auto start = std::chrono::steady_clock::now();
    auto result = sync_wait(task);
    auto end = std::chrono::steady_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    EXPECT_GE(duration, test_timer_timeout) << "sync_wait should wait for the event to be set";
    EXPECT_EQ(result, 42) << "sync_wait should return 42 after the event is set";

    EXPECT_TRUE(ev.is_set()) << "Event should be set after sync_wait completes";
}

TEST_CASE(TestTaskWithContinuation)
{
    auto async_fn = [&]() -> task<int>
    {
        co_return 42;
    };

    auto task = async_fn() | then([](int value)
                                  { return value + 1; });

    auto result = sync_wait(task);
    EXPECT_EQ(result, 43) << "sync_wait should return 42 after the event is set";

    event_signal signal;

    auto task2 = async_fn() | then([&signal](auto value)
                                   { signal.set(); }) |
                 then([]
                      { return 100; });

    auto result2 = sync_wait(task2);
    EXPECT_EQ(result2, 100) << "sync_wait should return 100 after the continuation is executed";
    EXPECT_TRUE(signal.is_set()) << "Signal should be set after the continuation is executed";
}

TEST_CASE(TestTaskWithErrorHandling)
{
    auto throws = []() -> task<int>
    {
        throw std::runtime_error("Test exception");
        co_return 42; // This line should never be reached
    };

    EXPECT_THROW(sync_wait(throws()), std::runtime_error) << "sync_wait should throw std::runtime_error";

    EXPECT_EQ(sync_wait(throws() | upon_error([](std::exception_ptr e)
                                              { return -1; })),
              -1)
        << "Task should handle the error and return -1";
}

TEST_CASE(TestTaskEagerness)
{
    event_signal signal;

    auto async_fn = [&]() -> task<void>
    {
        signal.set();
        co_return;
    };

    auto _ = async_fn();

    EXPECT_TRUE(signal.is_set()) << "Signal should be set immediately after task creation";
}

struct resume_on_thread_with_test_timer_timeout
{
    std::chrono::milliseconds test_timer_timeout;

    bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> h) const noexcept
    {
        std::thread([h, this]()
                    {
            std::this_thread::sleep_for(test_timer_timeout);
            h.resume(); })
            .detach();
    }

    void await_resume() const noexcept {}
};

TEST_CASE(TestTaskWhenAllVoid)
{
    constexpr std::chrono::milliseconds test_timer_timeout_1(500);
    constexpr std::chrono::milliseconds test_timer_timeout_2(300);

    event_signal signal1, signal2;

    auto task1 = [&]() -> task<void>
    {
        co_await resume_on_thread_with_test_timer_timeout{test_timer_timeout_1};
        signal1.set();
        co_return;
    };

    auto task2 = [&]() -> task<void>
    {
        co_await resume_on_thread_with_test_timer_timeout{test_timer_timeout_2};
        signal2.set();
        co_return;
    };

    auto start = std::chrono::steady_clock::now();
    std::vector<task<void>> tasks;
    tasks.emplace_back(task1());
    tasks.emplace_back(task2());

    sync_wait(when_all(tasks));
    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    EXPECT_TRUE(signal1.is_set()) << "First signal should be set after when_all completes";
    EXPECT_TRUE(signal2.is_set()) << "Second signal should be set after when_all completes";
    EXPECT_GE(duration, test_timer_timeout_1) << "when_all should wait for the longest task to complete";
}

TEST_CASE(TestTaskWhenAllHomogenous)
{
    constexpr std::chrono::milliseconds test_timer_timeout_1(500);
    constexpr std::chrono::milliseconds test_timer_timeout_2(300);

    auto task1 = [&]() -> task<int>
    {
        co_await resume_on_thread_with_test_timer_timeout{test_timer_timeout_1};

        co_return 1;
    };

    auto task2 = [&]() -> task<int>
    {
        co_await resume_on_thread_with_test_timer_timeout{test_timer_timeout_2};
        co_return 2;
    };

    auto start = std::chrono::steady_clock::now();

    std::vector<task<int>> tasks;
    tasks.emplace_back(task1());
    tasks.emplace_back(task2());
    auto results = sync_wait(when_all(tasks));

    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    EXPECT_EQ(results.size(), 2) << "when_all should return a vector of results";
    EXPECT_EQ(results[0], 1) << "First result should be 1";
    EXPECT_EQ(results[1], 2) << "Second result should be 2";

    EXPECT_GE(duration, test_timer_timeout_1) << "when_all should wait for the longest task to complete";
}

TEST_CASE(TestTaskWhenAllHeterogenous)
{
    constexpr std::chrono::milliseconds test_timer_timeout_1(500);
    constexpr std::chrono::milliseconds test_timer_timeout_2(300);
    constexpr std::chrono::milliseconds test_timer_timeout_3(800);
    event_signal signal;

    auto task1 = [&]() -> task<int>
    {
        co_await resume_on_thread_with_test_timer_timeout{test_timer_timeout_1};
        co_return 1;
    };

    auto task2 = [&]() -> task<std::string>
    {
        co_await resume_on_thread_with_test_timer_timeout{test_timer_timeout_2};
        co_return "two";
    };

    auto task3 = [&]() -> task<void>
    {
        co_await resume_on_thread_with_test_timer_timeout{test_timer_timeout_3};
        signal.set();
        co_return;
    };

    auto start = std::chrono::steady_clock::now();

    auto results = sync_wait(when_all(std::make_tuple(task1(), task2(), task3())));

    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    EXPECT_EQ(std::get<0>(results), 1) << "First result should be 1";
    EXPECT_EQ(std::get<1>(results), "two") << "Second result should be 'two'";
    EXPECT_TRUE(signal.is_set()) << "Signal should be set after the third task completes";

    EXPECT_GE(duration, test_timer_timeout_3) << "when_all should wait for the longest task to complete";
}

TEST_CASE(TestTaskWhenAnyVoid)
{
    constexpr std::chrono::milliseconds test_timer_timeout_1(500);
    constexpr std::chrono::milliseconds test_timer_timeout_2(300);

    event_signal signal1, signal2;

    auto task1 = [&]() -> task<void>
    {
        co_await resume_on_thread_with_test_timer_timeout{test_timer_timeout_1};
        signal1.set();
        co_return;
    };

    auto task2 = [&]() -> task<void>
    {
        co_await resume_on_thread_with_test_timer_timeout{test_timer_timeout_2};
        signal2.set();
        co_return;
    };

    auto start = std::chrono::steady_clock::now();
    std::vector<task<void>> tasks;
    tasks.emplace_back(task1());
    tasks.emplace_back(task2());

    sync_wait(when_any(tasks));
    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    EXPECT_TRUE(signal2.is_set()) << "At least one signal should be set after when_any completes";
    EXPECT_TRUE(!signal1.is_set()) << "Only the first task should complete, as it is the shortest";
    EXPECT_GE(duration, test_timer_timeout_2) << "when_any should wait for the shortest task to complete";
}

TEST_CASE(TestTaskWhenAnyReturnType)
{
    constexpr std::chrono::milliseconds test_timer_timeout_1(500);
    constexpr std::chrono::milliseconds test_timer_timeout_2(300);

    auto task1 = [&]() -> task<int>
    {
        co_await resume_on_thread_with_test_timer_timeout{test_timer_timeout_1};
        co_return 5;
    };

    auto task2 = [&]() -> task<int>
    {
        co_await resume_on_thread_with_test_timer_timeout{test_timer_timeout_2};
        co_return 3;
    };

    auto start = std::chrono::steady_clock::now();
    std::vector<task<int>> tasks;
    tasks.emplace_back(task1());
    tasks.emplace_back(task2());

    auto result = sync_wait(when_any(tasks));
    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    EXPECT_EQ(result, 3) << "when_any should return the result of the first completed task";
    EXPECT_GE(duration, test_timer_timeout_2) << "when_any should wait for the shortest task to complete";
}

TEST_CASE(TestTaskWhenAnyHeterogeneous)
{
    constexpr std::chrono::milliseconds test_timer_timeout_1(500);
    constexpr std::chrono::milliseconds test_timer_timeout_2(300);
    constexpr std::chrono::milliseconds test_timer_timeout_3(800);

    event_signal signal;

    auto task1 = [&]() -> task<int>
    {
        co_await resume_on_thread_with_test_timer_timeout{test_timer_timeout_1};
        co_return 1;
    };

    auto task2 = [&]() -> task<std::string>
    {
        co_await resume_on_thread_with_test_timer_timeout{test_timer_timeout_2};
        co_return "two";
    };

    auto task3 = [&]() -> task<void>
    {
        co_await resume_on_thread_with_test_timer_timeout{test_timer_timeout_3};
        signal.set();
        co_return;
    };

    auto start = std::chrono::steady_clock::now();

    auto result = sync_wait(when_any(task1(), task2(), task3()));

    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    EXPECT_TRUE(std::holds_alternative<std::string>(result)) << "Result should be either int or string";
    EXPECT_EQ(std::get<std::string>(result), "two") << "Second result should be 'two'";

    EXPECT_GE(duration, test_timer_timeout_2) << "when_any should wait for the shortest task to complete";
}