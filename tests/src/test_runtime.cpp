
#define TEST_SUITE_NAME RuntimeTestSuite

#include "test_suite.hpp"
#include <async/event_signal.h>
#include <webcraft/async/runtime/runtime.hpp>
#include <thread>
#include <chrono>

using namespace std::chrono_literals;

::async::task<void> lightweight_yield()
{
    struct lightweight_yield_awaiter
    {
        constexpr void await_resume() const noexcept {}
        constexpr bool await_ready() const noexcept { return false; }
        void await_suspend(std::coroutine_handle<> h) const noexcept
        {
            std::this_thread::sleep_for(200ms);
            h.resume();
        }
    };

    co_await lightweight_yield_awaiter{};
}

::async::task<void> async_lightweight_yield_control()
{

    int value = 5;

    co_await lightweight_yield();
    std::cout << "Hello " << std::endl;
    EXPECT_EQ(value, 5) << "Value should remain unchanged after yielding control";

    value = 6;
    co_await lightweight_yield();
    EXPECT_EQ(value, 6) << "Value should remain unchanged after yielding control again";
}

TEST_CASE(test_lightweight_yield_control)
{

    auto task = async_lightweight_yield_control();

    async::awaitable_get(task);
}

::async::task<void> yield(HANDLE iocp)
{

    struct yield_awaiter : public std::enable_share
    {
        HANDLE iocp;

        yield_awaiter(HANDLE iocp) : iocp(iocp) {}

        constexpr void await_resume() const {}
        constexpr bool await_ready() const noexcept { return false; }
        void await_suspend(std::coroutine_handle<> handle) noexcept
        {
            webcraft::async::runtime::win32::post_nop_event(iocp, reinterpret_cast<uint64_t>(handle.address()));
        }
    };

    co_await yield_awaiter{iocp};

}

::async::task<void> async_test_yield_control()
{

    int value = 5;

    const auto iocp = webcraft::async::runtime::provider->iocp;
    co_await yield(iocp);
    EXPECT_EQ(value, 5) << "Value should remain unchanged after yielding control";

    value = 6;
    co_await yield(iocp);
    EXPECT_EQ(value, 6) << "Value should remain unchanged after yielding control again";
}

TEST_CASE(test_yield_control)
{
    auto task = async_test_yield_control();

    auto payload = webcraft::async::runtime::wait_and_get_event();
    std::coroutine_handle<>::from_address(reinterpret_cast<void *>(payload)).resume();

    payload = webcraft::async::runtime::wait_and_get_event();
    std::coroutine_handle<>::from_address(reinterpret_cast<void *>(payload)).resume();

    async::awaitable_get(task);
}

// ::async::task<void> async_test_sleep_for()
// {
//     // Test Yield
//     co_await webcraft::async::runtime::yield(); // Yield to allow the runtime to process the sleep

//     constexpr auto sleep_time = std::chrono::seconds(5);
//     constexpr auto cancel_time = std::chrono::seconds(2);

//     auto start = std::chrono::steady_clock::now();
//     co_await webcraft::async::runtime::sleep_for(sleep_time);
//     auto end = std::chrono::steady_clock::now();

//     auto elapsed_time = std::chrono::duration_cast<std::chrono::seconds>(end - start);
//     EXPECT_GE(elapsed_time, sleep_time)
//         << "Expected sleep to last at least the specified sleep time, but it did not";

//     std::stop_source source;
//     std::stop_token token = source.get_token();

//     std::jthread cancel_thread([&source, cancel_time]()
//                                {
//         std::this_thread::sleep_for(cancel_time);
//         source.request_stop(); });

//     start = std::chrono::steady_clock::now();
//     co_await webcraft::async::runtime::sleep_for(sleep_time, token);
//     end = std::chrono::steady_clock::now();

//     auto cancel_time_elapsed = std::chrono::duration_cast<std::chrono::seconds>(end - start);

//     EXPECT_TRUE(token.stop_requested())
//         << "Expected cancellation to be requested, but it was not";

//     EXPECT_GE(cancel_time_elapsed, cancel_time)
//         << "Expected cancellation to occur after the cancel time, but it did not";
//     EXPECT_LE(cancel_time_elapsed, sleep_time)
//         << "Expected cancellation to occur before the sleep time, but it did not";
// }

// TEST_CASE(test_sleep_for)
// {
//     auto task = async_test_sleep_for();

//     auto provider = webcraft::async::runtime::detail::get_runtime_provider();
//     auto event = provider->wait_and_get_event();

//     if (event)
//     {
//         event->try_resume();
//     }

//     event = provider->wait_and_get_event();
//     if (event)
//     {
//         event->try_resume();
//     }

//     event = provider->wait_and_get_event();
//     if (event)
//     {
//         event->try_resume();
//     }

//     async::awaitable_get(task);
// }

// #define ASYNC_TEST_CASE(name)                        \
//     ::async::task<void> async_##name();              \
//     TEST_CASE(name)                                  \
//     {                                                \
//         webcraft::async::runtime::run(async_##name); \
//     }                                                \
//     ::async::task<void> async_##name()

// ASYNC_TEST_CASE(runtime_test_yield_control)
// {
//     co_await async_test_yield_control();
// }

// ASYNC_TEST_CASE(runtime_test_sleep_for)
// {
//     co_await async_test_sleep_for();
// }