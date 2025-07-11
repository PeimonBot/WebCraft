#ifdef __linux__

#define TEST_SUITE_NAME IOUringTestSuite
#include "test_suite.hpp"
#include <liburing.h>

TEST_CASE(SampleTest)
{
    EXPECT_EQ(1 + 1, 2);
}

void initialize_io_uring(struct io_uring *ring)
{
    int ret = io_uring_queue_init(8, ring, 0);
    EXPECT_EQ(ret, 0) << "Failed to initialize io_uring:" << ret;
}

void destroy_io_uring(struct io_uring *ring)
{
    io_uring_queue_exit(ring);
}

// Test case to initialize and clean up io_uring
TEST_CASE(io_uring_setup_and_cleanup)
{
    struct io_uring ring;
    initialize_io_uring(&ring);

    // Cleanup
    destroy_io_uring(&ring);
}

void post_nop_event(struct io_uring *ring, uint64_t payload)
{
    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    EXPECT_NE(sqe, nullptr) << "Failed to get SQE from io_uring";

    // Prepare a NOP operation to signal the completion of the task
    io_uring_prep_nop(sqe);
    io_uring_sqe_set_data64(sqe, payload);
    int ret = io_uring_submit(ring);
    EXPECT_GE(ret, 0) << "Failed to submit SQE to io_uring: " << ret;
}

auto wait_and_get_event(struct io_uring *ring)
{
    struct io_uring_cqe *cqe;
    int ret = io_uring_wait_cqe(ring, &cqe);
    EXPECT_EQ(ret, 0) << "Failed to wait for completion event:" << ret;

    // Process the completion event
    EXPECT_NE(cqe, nullptr) << "Completion event is null";
    auto user_data = cqe->user_data;
    io_uring_cqe_seen(ring, cqe);
    return user_data;
}

void wait_for_event(struct io_uring *ring, uint64_t payload)
{
    auto user_data = wait_and_get_event(ring);

    EXPECT_EQ(user_data, payload) << "Payload mismatch in completion event. Expected: " << payload << ", Got: " << user_data;
}

// Test cases for posting and waiting for events with no payload
TEST_CASE(io_uring_post_and_wait_for_event_no_payload)
{
    struct io_uring ring;
    initialize_io_uring(&ring);

    // Post an event
    post_nop_event(&ring, 0);

    // Wait for the event to complete
    wait_for_event(&ring, 0);

    // Cleanup
    destroy_io_uring(&ring);
}

// Test case for posting and waiting for an event with a specific payload
TEST_CASE(io_uring_post_and_wait_for_event_random_payload)
{
    struct io_uring ring;
    initialize_io_uring(&ring);

    uint64_t random_payload = generate_random_uint64();

    // Post an event
    post_nop_event(&ring, random_payload);

    // Wait for the event to complete
    wait_for_event(&ring, random_payload);

    // Cleanup
    destroy_io_uring(&ring);
}

// Test case for posting a callback and executing it when the event is completed
TEST_CASE(io_uring_post_and_test_callback)
{
    struct io_uring ring;
    initialize_io_uring(&ring);

    std::atomic<bool> callback_called;

    struct callback_event
    {
        std::atomic<bool> *callback_flag;

        void operator()()
        {
            callback_flag->store(true); // Set the callback flag to true
        }
    };

    callback_event cb_event{&callback_called};

    post_nop_event(&ring, reinterpret_cast<uint64_t>(&cb_event));

    callback_event *ptr = reinterpret_cast<callback_event *>(wait_and_get_event(&ring));

    (*ptr)(); // Call the callback

    EXPECT_TRUE(callback_called.load()) << "Callback was not called";

    // Cleanup
    destroy_io_uring(&ring);
}

// Test case for waiting for multiple events
TEST_CASE(io_uring_wait_multiple_events)
{
    // Define the number of events to wait for
    constexpr int num_events = 5;

    // initialize the structures
    std::atomic<bool> signals[num_events];
    struct io_uring ring;
    initialize_io_uring(&ring);

    // the callback event structure
    struct callback_event
    {
        std::atomic<bool> *callback_flag;

        void operator()()
        {
            callback_flag->store(true); // Set the callback flag to true
        }
    };

    // spawn and post multiple callback events
    for (int i = 0; i < num_events; ++i)
    {
        callback_event *cb_event = new callback_event{&signals[i]};
        post_nop_event(&ring, reinterpret_cast<uint64_t>(cb_event));
    }

    // Wait for all events to complete and call the callbacks
    for (int i = 0; i < num_events; ++i)
    {
        callback_event *ptr = reinterpret_cast<callback_event *>(wait_and_get_event(&ring));
        (*ptr)();   // Call the callback
        delete ptr; // Clean up the callback event
    }

    // Check if all signals were set
    for (int i = 0; i < num_events; ++i)
    {
        EXPECT_TRUE(signals[i].load()) << "Callback " << i << " was not called";
    }

    // Cleanup
    destroy_io_uring(&ring);
}

void post_timer_event(struct io_uring *ring, std::chrono::steady_clock::duration duration, uint64_t payload)
{
    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    EXPECT_NE(sqe, nullptr) << "Failed to get SQE from io_uring";

    // Prepare a timer event

    struct __kernel_timespec its;
    its.tv_sec = std::chrono::duration_cast<std::chrono::seconds>(duration).count();
    its.tv_nsec = std::chrono::duration_cast<std::chrono::nanoseconds>(duration % std::chrono::seconds(1)).count();

    io_uring_prep_timeout(sqe, &its, 0, 0);
    io_uring_sqe_set_data64(sqe, payload);
    int ret = io_uring_submit(ring);
    EXPECT_GE(ret, 0) << "Failed to submit SQE to io_uring: " << ret;
}

void wait_for_timeout_event(struct io_uring *ring, uint64_t payload)
{
    struct io_uring_cqe *cqe;
    int ret = io_uring_wait_cqe(ring, &cqe);
    EXPECT_EQ(ret, 0) << "Failed to wait for completion event:" << ret;

    // Process the completion event
    EXPECT_NE(cqe, nullptr) << "Completion event is null";
    EXPECT_GE(cqe->res, -ETIME) << "Completion event result is negative: " << cqe->res;

    EXPECT_EQ(cqe->user_data, payload) << "Payload mismatch in completion event. Expected: " << payload << ", Got: " << cqe->user_data;

    io_uring_cqe_seen(ring, cqe);
}

TEST_CASE(io_uring_test_timer)
{
    constexpr int payload = 0;

    struct io_uring ring;
    initialize_io_uring(&ring);

    auto sleep_time = std::chrono::seconds(5);

    post_timer_event(&ring, sleep_time, payload);

    auto start = std::chrono::steady_clock::now();
    wait_for_timeout_event(&ring, payload);
    auto end = std::chrono::steady_clock::now();

    auto elapsed_time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    EXPECT_GE(elapsed_time, sleep_time) << "Timer event did not complete after the expected duration";

    destroy_io_uring(&ring);
}

// ::async::task<void> test_yield_coroutine(struct io_uring *ring)
// {
//     struct yield_awaiter
//     {
//         struct io_uring *ring;

//         constexpr void await_resume() const noexcept {}
//         constexpr bool await_ready() const noexcept { return false; }
//         void await_suspend(std::coroutine_handle<> handle) const noexcept
//         {
//             // Post the overlapped event to the io_uring
//             post_nop_event(ring, reinterpret_cast<uint64_t>(handle.address()));
//         }
//     };

//     int value = 5;
//     co_await yield_awaiter{ring};
//     EXPECT_EQ(value, 5) << "Value should remain unchanged in the coroutine";

//     value = 6;
//     co_await yield_awaiter{ring};
//     EXPECT_EQ(value, 6) << "Value should be updated in the coroutine";
// }

// TEST_CASE(runtime_yield_test_coroutine)
// {
//     struct io_uring ring;
//     initialize_io_uring(&ring);

//     test_yield_coroutine(&ring);

//     auto payload = wait_and_get_event(&ring);
//     std::coroutine_handle<>::from_address(reinterpret_cast<void *>(payload)).resume();

//     payload = wait_and_get_event(&ring);
//     std::coroutine_handle<>::from_address(reinterpret_cast<void *>(payload)).resume();

//     destroy_io_uring(&ring);
// }

// ::async::task<void> test_timer_coroutine(struct io_uring *ring)
// {
//     struct timer_awaiter
//     {
//         struct io_uring *ring;
//         std::chrono::steady_clock::duration duration;

//         constexpr void await_resume() const noexcept {}
//         constexpr bool await_ready() const noexcept { return false; }
//         void await_suspend(std::coroutine_handle<> handle) const noexcept
//         {
//             // Post the overlapped event to the IOCP
//             post_timer_event(ring, duration, reinterpret_cast<uint64_t>(handle.address()));
//         }
//     };

//     constexpr auto sleep_time = std::chrono::seconds(5);

//     auto start = std::chrono::steady_clock::now();
//     co_await timer_awaiter{ring, sleep_time};
//     auto end = std::chrono::steady_clock::now();

//     auto elapsed_time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

//     EXPECT_GE(elapsed_time, sleep_time) << "Timer event did not complete after the expected duration";

//     start = std::chrono::steady_clock::now();
//     co_await timer_awaiter{ring, sleep_time};

//     end = std::chrono::steady_clock::now();
//     elapsed_time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
//     EXPECT_GE(elapsed_time, sleep_time) << "Timer event did not complete after the expected duration";
// }

// TEST_CASE(runtime_test_timer_coroutine)
// {
//     struct io_uring ring;
//     initialize_io_uring(&ring);

//     test_timer_coroutine(&ring);

//     auto payload = wait_and_get_event(&ring);
//     std::coroutine_handle<>::from_address(reinterpret_cast<void *>(payload)).resume();

//     payload = wait_and_get_event(&ring);
//     std::coroutine_handle<>::from_address(reinterpret_cast<void *>(payload)).resume();

//     destroy_io_uring(&ring);
// }

#endif