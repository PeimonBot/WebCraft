#ifdef _WIN32

#define TEST_SUITE_NAME IOCPTestSuite
#include "test_suite.hpp"
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <WinSock2.h>
#include <webcraft/async/runtime/windows/windows_timer_manager.hpp>

using namespace std::chrono_literals;

TEST_CASE(SampleTest)
{
    EXPECT_EQ(1 + 1, 2);
}

HANDLE initialize_iocp()
{
    // Initialize Winsock
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    EXPECT_EQ(result, 0) << "Failed to initialize Winsock: " << result;

    // Create an IO Completion Port (IOCP)
    auto iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    HANDLE null_handle = NULL;
    EXPECT_NE(iocp, null_handle) << "Failed to create IOCP";
    return iocp;
}

void destroy_iocp(HANDLE iocp)
{
    auto check = CloseHandle(iocp);
    EXPECT_TRUE(check) << "Failed to close IOCP handle";

    // Cleanup Winsock
    int result = WSACleanup();
    EXPECT_EQ(result, 0) << "Failed to cleanup Winsock: " << result;
}

// Test case to initialize and clean up IOCP
TEST_CASE(iocp_setup_and_cleanup)
{
    HANDLE iocp = initialize_iocp();

    // Cleanup
    destroy_iocp(iocp);
}

struct overlapped_event : public OVERLAPPED
{
    uint64_t payload;
};

void post_nop_event(HANDLE iocp, uint64_t payload)
{
    // Post a NOP operation to the IOCP
    overlapped_event *overlapped = new overlapped_event();
    memset(overlapped, 0, sizeof(OVERLAPPED));
    overlapped->payload = payload;

    BOOL result = PostQueuedCompletionStatus(iocp, 0, payload, overlapped);
    EXPECT_TRUE(result) << "Failed to post NOP event to IOCP";
}

auto wait_and_get_event(HANDLE iocp)
{
    DWORD bytesTransferred;
    ULONG_PTR completionKey;
    OVERLAPPED *overlapped;

    BOOL result = GetQueuedCompletionStatus(iocp, &bytesTransferred, &completionKey, &overlapped, INFINITE);
    EXPECT_TRUE(result) << "Failed to get completion status from IOCP";

    // Process the completion event
    EXPECT_NE(overlapped, nullptr) << "Completion event is null";
    auto event = static_cast<overlapped_event *>(overlapped);
    uint64_t user_data = event->payload;

    delete event; // Clean up the overlapped structure
    return user_data;
}

void wait_for_event(HANDLE iocp, uint64_t expected_payload)
{
    auto payload = wait_and_get_event(iocp);
    EXPECT_EQ(payload, expected_payload) << "Payload mismatch in completion event. Expected: " << expected_payload << ", Got: " << payload;
}

// Test cases for posting and waiting for events with no payload
TEST_CASE(iocp_post_and_wait_for_event_no_payload)
{
    HANDLE iocp = initialize_iocp();

    // Post an event
    post_nop_event(iocp, 0);

    // Wait for the event to complete
    wait_for_event(iocp, 0);

    // Cleanup
    destroy_iocp(iocp);
}

// Test case for posting and waiting for an event with a specific payload
TEST_CASE(iocp_post_and_wait_for_event_random_payload)
{
    HANDLE iocp = initialize_iocp();

    uint64_t random_payload = generate_random_uint64();

    // Post an event
    post_nop_event(iocp, random_payload);

    // Wait for the event to complete
    wait_for_event(iocp, random_payload);

    // Cleanup
    destroy_iocp(iocp);
}

// Test case for posting a callback and executing it when the event is completed
TEST_CASE(iocp_post_and_test_callback)
{
    HANDLE iocp = initialize_iocp();

    // Create a signal to test the callback
    async::event_signal signal;

    struct callback_event
    {
        async::event_signal *signal;

        void operator()()
        {
            signal->set(); // Set the signal to indicate the callback was called
        }
    };

    callback_event cb_event{&signal};

    // Post an event with a callback
    post_nop_event(iocp, reinterpret_cast<uint64_t>(&cb_event));

    // Wait for the event to complete and call the callback
    callback_event *ptr = reinterpret_cast<callback_event *>(wait_and_get_event(iocp));

    (*ptr)(); // Call the callback

    // Check if the signal was set
    EXPECT_TRUE(signal.is_set()) << "Callback was not called";

    // Cleanup
    destroy_iocp(iocp);
}

// Test case for waiting for multiple events
TEST_CASE(iocp_wait_multiple_events)
{
    // Define the number of events to wait for
    constexpr int num_events = 5;

    // Initialize the IOCP
    HANDLE iocp = initialize_iocp();

    // Create an array of signals
    async::event_signal signals[num_events];

    // the callback event structure
    struct callback_event
    {
        async::event_signal *signal;

        void operator()()
        {
            signal->set(); // Set the signal to indicate the callback was called
        }
    };

    // Post multiple callback events
    for (int i = 0; i < num_events; ++i)
    {
        callback_event *cb_event = new callback_event{&signals[i]};
        post_nop_event(iocp, reinterpret_cast<uint64_t>(cb_event));
    }

    // Wait for all events to complete and call the callbacks
    for (int i = 0; i < num_events; ++i)
    {
        callback_event *ptr = reinterpret_cast<callback_event *>(wait_and_get_event(iocp));
        (*ptr)();   // Call the callback
        delete ptr; // Clean up the callback event
    }

    // Check if all signals were set
    for (int i = 0; i < num_events; ++i)
    {
        EXPECT_TRUE(signals[i].is_set()) << "Callback " << i << " was not called";
    }

    // Cleanup
    destroy_iocp(iocp);
}

void post_timer_event(HANDLE iocp, webcraft::async::runtime::detail::timer_manager &context, std::chrono::steady_clock::duration duration, uint64_t payload)
{
    context.post_timer_event(duration, [iocp, payload]()
                             {
        // This callback will be called when the timer expires
        post_nop_event(iocp, payload); });
}

void wait_for_timeout_event(HANDLE iocp, uint64_t payload)
{
    std::cout << "Waiting for timer event with payload: " << payload << std::endl;
    wait_for_event(iocp, payload);
    std::cout << "Timer event completed with payload: " << payload << std::endl;
}

TEST_CASE(iocp_test_timer)
{
    constexpr int payload = 0;
    std::cout << "Testing IOCP timer functionality..." << std::endl;

    webcraft::async::runtime::detail::timer_manager manager;
    // Initialize the IOCP
    HANDLE iocp = initialize_iocp();

    auto sleep_time = std::chrono::seconds(5);
    std::cout << "Posting timer event with sleep time: " << sleep_time.count() << " seconds" << std::endl;

    post_timer_event(iocp, manager, sleep_time, payload);

    auto start = std::chrono::steady_clock::now();
    wait_for_timeout_event(iocp, payload);
    auto end = std::chrono::steady_clock::now();

    auto elapsed_time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start + 100ms);

    EXPECT_GE(elapsed_time, sleep_time) << "Timer event did not complete after the expected duration";

    destroy_iocp(iocp);
}

::async::task<void> test_yield_coroutine(HANDLE iocp)
{
    struct yield_awaiter
    {
        HANDLE iocp;

        constexpr void await_resume() const noexcept {}
        constexpr bool await_ready() const noexcept { return false; }
        void await_suspend(std::coroutine_handle<> handle) const noexcept
        {
            // Post the overlapped event to the IOCP
            post_nop_event(iocp, reinterpret_cast<uint64_t>(handle.address()));
        }
    };

    int value = 5;
    co_await yield_awaiter{iocp};
    EXPECT_EQ(value, 5) << "Value should remain unchanged in the coroutine";

    value = 6;
    co_await yield_awaiter{iocp};
    EXPECT_EQ(value, 6) << "Value should be updated in the coroutine";
}

TEST_CASE(runtime_test_yield_coroutine)
{

    HANDLE iocp = initialize_iocp();

    test_yield_coroutine(iocp);

    auto payload = wait_and_get_event(iocp);
    std::coroutine_handle<>::from_address(reinterpret_cast<void *>(payload)).resume();

    payload = wait_and_get_event(iocp);
    std::coroutine_handle<>::from_address(reinterpret_cast<void *>(payload)).resume();

    destroy_iocp(iocp);
}

::async::task<void> test_timer_coroutine(HANDLE iocp, webcraft::async::runtime::detail::timer_manager &manager)
{
    struct timer_awaiter
    {
        HANDLE iocp;
        std::chrono::steady_clock::duration duration;
        webcraft::async::runtime::detail::timer_manager &manager;

        constexpr void await_resume() const noexcept {}
        constexpr bool await_ready() const noexcept { return false; }
        void await_suspend(std::coroutine_handle<> handle) const noexcept
        {
            // Post the overlapped event to the IOCP
            post_timer_event(iocp, manager, duration, reinterpret_cast<uint64_t>(handle.address()));
        }
    };

    constexpr auto sleep_time = std::chrono::seconds(5);

    auto start = std::chrono::steady_clock::now();
    co_await timer_awaiter{iocp, sleep_time, manager};
    auto end = std::chrono::steady_clock::now();

    auto elapsed_time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start + 100ms);

    EXPECT_GE(elapsed_time, sleep_time) << "Timer event did not complete after the expected duration";

    start = std::chrono::steady_clock::now();
    co_await timer_awaiter{iocp, sleep_time, manager};

    end = std::chrono::steady_clock::now();
    elapsed_time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start + 100ms);
    EXPECT_GE(elapsed_time, sleep_time) << "Timer event did not complete after the expected duration";
}

TEST_CASE(runtime_test_timer_coroutine)
{

    HANDLE iocp = initialize_iocp();
    webcraft::async::runtime::detail::timer_manager manager;

    test_timer_coroutine(iocp, manager);

    auto payload = wait_and_get_event(iocp);
    std::coroutine_handle<>::from_address(reinterpret_cast<void *>(payload)).resume();

    payload = wait_and_get_event(iocp);
    std::coroutine_handle<>::from_address(reinterpret_cast<void *>(payload)).resume();

    destroy_iocp(iocp);
}

#endif
