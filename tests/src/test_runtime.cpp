#define TEST_SUITE_NAME RuntimeTestSuite

// #define WEBCRAFT_ASYNC_RUNTIME_MOCK

#include "test_suite.hpp"
#include <webcraft/async/async.hpp>

using namespace webcraft::async;

TEST_CASE(TestRuntimeInitAndDestroy)
{
    runtime_context context;
}

// TEST_CASE(TestSimpleRuntime)
// {
//     std::cout << "Starting TestSimpleRuntime..." << std::endl;
//     runtime_context context;

//     auto task_fn = []() -> task<void>
//     {
//         int count = 0;
//         for (int i = 0; i < 10; ++i)
//         {
//             int temp = count;
//             co_await yield();
//             EXPECT_EQ(temp, count) << "Count should remain the same during yield";
//             count++;
//         }
//         EXPECT_EQ(count, 10) << "Count should be 10 after yielding 10 times";
//     };

//     throw std::runtime_error("This is a test exception to ensure the runtime can handle exceptions properly");
//     sync_wait(task_fn());

//     std::cout << "TestSimpleRuntime completed successfully." << std::endl;
// }