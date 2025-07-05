
#define TEST_SUITE_NAME RuntimeTestSuite

#include "test_suite.hpp"
#include <webcraft/async/runtime/runtime.hpp>
#include <async/event_signal.h>

::async::task<void> async_test_yield_control()
{
    int value = 5;

    co_await webcraft::async::runtime::yield();
    EXPECT_EQ(value, 5) << "Value should remain unchanged after yielding control";

    value = 6;
    co_await webcraft::async::runtime::yield();
    EXPECT_EQ(value, 6) << "Value should remain unchanged after yielding control again";
}

TEST_CASE(test_yield_control)
{
    auto task = async_test_yield_control();

    auto provider = webcraft::async::runtime::detail::get_runtime_provider();
    auto event = provider->wait_and_get_event();

    if (event)
    {
        event->try_resume();
    }

    event = provider->wait_and_get_event();
    if (event)
    {
        event->try_resume();
    }

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