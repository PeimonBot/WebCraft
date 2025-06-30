
#if 0

#define TEST_SUITE_NAME RuntimeTestSuite

#include "test_suite.hpp"
#include <webcraft/async/runtime/runtime.hpp>
#include <async/event_signal.h>

TEST_CASE(runtime_setup_and_cleanup)
{
    auto provider = webcraft::async::runtime::detail::get_runtime_provider();
    EXPECT_TRUE(provider != nullptr) << "Runtime provider should not be null";

    EXPECT_NO_THROW(provider->setup_runtime()) << "Setup runtime should not throw";
    EXPECT_NO_THROW(provider->cleanup_runtime()) << "Cleanup runtime should not throw";
}

TEST_CASE(runtime_post_and_wait_for_event)
{
    auto provider = webcraft::async::runtime::detail::get_runtime_provider();
    EXPECT_TRUE(provider != nullptr) << "Runtime provider should not be null";

    EXPECT_NO_THROW(provider->setup_runtime()) << "Setup runtime should not throw";

    ::async::event_signal event_signal;

    // Post a NOP event
    auto nop_event = provider->create_nop_event([&event_signal]()
                                                { event_signal.set(); });
    nop_event->start_async();
    EXPECT_TRUE(nop_event != nullptr) << "NOP event should not be null";

    // Wait for the event to be ready
    auto event = provider->wait_and_get_event();
    EXPECT_TRUE(event != nullptr) << "Event should not be null";

    // Execute the callback
    event->execute_callback();

    EXPECT_TRUE(event_signal.is_set()) << "Event signal should be set after executing the callback";

    EXPECT_NO_THROW(provider->cleanup_runtime()) << "Cleanup runtime should not throw";
}

TEST_CASE(runtime_post_and_wait_for_multiple_events)
{
    auto provider = webcraft::async::runtime::detail::get_runtime_provider();
    EXPECT_TRUE(provider != nullptr) << "Runtime provider should not be null";

    EXPECT_NO_THROW(provider->setup_runtime()) << "Setup runtime should not throw";

    constexpr int num_events = 5;
    std::vector<::async::event_signal> event_signals(num_events);
    std::vector<std::unique_ptr<webcraft::async::runtime::detail::native_runtime_event>> events(num_events);

    // Post multiple NOP events
    for (int i = 0; i < num_events; ++i)
    {
        auto nop_event = provider->create_nop_event([&event_signals, i]()
                                                    { event_signals[i].set(); });
        EXPECT_TRUE(nop_event != nullptr) << "NOP event should not be null";
        nop_event->start_async();
        events[i] = std::move(nop_event);
    }

    // Wait for all events to complete and call the callbacks
    for (int i = 0; i < num_events; ++i)
    {
        auto event = provider->wait_and_get_event();
        EXPECT_TRUE(event != nullptr) << "Event should not be null";
        event->execute_callback();
    }

    // Check if all signals were set
    for (int i = 0; i < num_events; ++i)
    {
        EXPECT_TRUE(event_signals[i].is_set()) << "Callback " << i << " was not called";
    }

    EXPECT_NO_THROW(provider->cleanup_runtime()) << "Cleanup runtime should not throw";
}

TEST_CASE(runtime_post_timer_event)
{
    std::cerr << "Starting timer event test..." << std::endl;
    auto provider = webcraft::async::runtime::detail::get_runtime_provider();
    EXPECT_NE(provider, nullptr) << "Runtime provider should not be null";

    EXPECT_NO_THROW(provider->setup_runtime()) << "Setup runtime should not throw";

    constexpr auto duration = std::chrono::seconds(1);

    std::cerr << "Posting timer event with duration: " << duration.count() << " seconds" << std::endl;
    ::async::event_signal signal;
    auto timer_event = provider->create_timer_event(duration, [&signal]()
                                                    { signal.set(); });

    EXPECT_NE(timer_event, nullptr) << "Timer event should not be null";

    timer_event->start_async();

    auto start_time = std::chrono::steady_clock::now();
    // Wait for the timer event to complete
    auto event = provider->wait_and_get_event();

    auto end_time = std::chrono::steady_clock::now();
    EXPECT_NE(event, nullptr) << "Event should not be null";

    // Execute the callback
    event->execute_callback();
    EXPECT_TRUE(signal.is_set()) << "Signal should be set after timer event execution";

    EXPECT_GE(end_time - start_time, duration) << "Timer event should have waited for the specified duration";

    EXPECT_NO_THROW(provider->cleanup_runtime()) << "Cleanup runtime should not throw";
}

#define ASYNC_TEST_CASE(name)                        \
    ::async::task<void> async_##name();              \
    TEST_CASE(name)                                  \
    {                                                \
        webcraft::async::runtime::run(async_##name); \
    }                                                \
    ::async::task<void> async_##name()

struct lightweight_yield_awaiter
{
    constexpr bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> handle) const noexcept
    {
        // Simply resume the coroutine immediately
        handle.resume();
    }
    void await_resume() const noexcept {}
};

ASYNC_TEST_CASE(runtime_test_lightweight_yield)
{
    int value = 5;

    co_await lightweight_yield_awaiter();
    EXPECT_EQ(value, 5) << "Value should remain unchanged after lightweight yield";

    value = 6;
    co_await lightweight_yield_awaiter();
    EXPECT_EQ(value, 6) << "Value should remain unchanged after another lightweight yield";
}

ASYNC_TEST_CASE(runtime_test_yield_control)
{
    int value = 5;

    co_await webcraft::async::runtime::yield();
    EXPECT_EQ(value, 5) << "Value should remain unchanged after yielding control";

    value = 6;
    co_await webcraft::async::runtime::yield();
    EXPECT_EQ(value, 6) << "Value should remain unchanged after yielding control again";
}

#endif