

#define TEST_SUITE_NAME TaskTestSuite

#include "test_suite.hpp"
#include <webcraft/async/task.hpp>
#include <string>
#include <webcraft/async/async_event.hpp>

using namespace webcraft::async;

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
static_assert(std::same_as<::awaitable_resume_t<awaitable_with_co_await>, void>, "awaitable_with_co_await should resume to void");

struct awaitable_with_awaitable_elements
{
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<>) const noexcept {}
    void await_resume() const noexcept {}
};

static_assert(awaitable_t<awaitable_with_awaitable_elements>, "awaitable_with_awaitable_elements should be considered awaitable");
static_assert(std::same_as<::awaitable_resume_t<awaitable_with_awaitable_elements>, void>, "awaitable_with_co_await should resume to void");

struct awaitable_outside
{
};

auto operator co_await(awaitable_outside) noexcept
{
    return std::suspend_always{};
}

static_assert(awaitable_t<awaitable_outside>, "awaitable_outside should be considered awaitable");
static_assert(std::same_as<::awaitable_resume_t<awaitable_outside>, void>, "awaitable_with_co_await should resume to void");

// ensuring that awaitable with a resume type is correctly identified
struct awaitable_with_resume_type_int
{
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<>) const noexcept {}
    int await_resume() const noexcept { return 42; }
};

static_assert(awaitable_t<awaitable_with_resume_type_int>, "awaitable_with_resume_type_int should be considered awaitable");
static_assert(std::same_as<::awaitable_resume_t<awaitable_with_resume_type_int>, int>, "awaitable_with_resume_type_int should resume to int");

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

constexpr std::chrono::milliseconds timeout(500);

TEST_CASE(TestingSyncWaitWithAnotherThread)
{

    struct thread_awaitable
    {
        bool await_ready() const noexcept { return false; }
        void await_suspend(std::coroutine_handle<> h) const noexcept
        {
            std::thread([h]()
                        { std::this_thread::sleep_for(timeout); h.resume(); })
                .detach();
        }
        void await_resume() const noexcept {}
    };

    auto asyncfn = []() -> task<void>
    {
        co_await thread_awaitable();
    };

    auto start = std::chrono::steady_clock::now();
    sync_wait(asyncfn());
    auto end = std::chrono::steady_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    EXPECT_GE(duration, timeout) << "sync_wait should wait for the thread to finish";
    EXPECT_LE(duration, timeout + std::chrono::milliseconds(100)) << "sync_wait should not wait too long";
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
        std::this_thread::sleep_for(timeout);
        ev.set(); })
        .detach();

    auto start = std::chrono::steady_clock::now();
    auto result = sync_wait(task);
    auto end = std::chrono::steady_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    EXPECT_GE(duration, timeout) << "sync_wait should wait for the event to be set";
    EXPECT_LE(duration, timeout + std::chrono::milliseconds(100)) << "sync_wait should not wait too long";
    EXPECT_EQ(result, 42) << "sync_wait should return 42 after the event is set";

    EXPECT_TRUE(ev.is_set()) << "Event should be set after sync_wait completes";
}