
#define TEST_SUITE_NAME STDEXECTestSuite

#include "test_suite.hpp"

#include <stdexec/execution.hpp>
#include <cstdio>
#include <string>
#include <thread>
#include <utility>
#include <exec/task.hpp>
#include <exec/inline_scheduler.hpp>
#include <webcraft/async/runtime.hpp>

using namespace std::literals;

TEST_CASE(stdexec_standard_test)
{

    stdexec::run_loop loop;

    std::jthread worker([&](std::stop_token st)
                        {
        std::stop_callback cb{st, [&]{ loop.finish(); }};
        loop.run(); });

    auto string = "hello world"s;

    stdexec::sender auto hello = stdexec::just(string);
    stdexec::sender auto print = std::move(hello) | stdexec::then([](std::string msg)
                                                                  { return std::puts(msg.c_str()); });

    stdexec::scheduler auto io_thread = loop.get_scheduler();
    stdexec::sender auto work = stdexec::on(io_thread, std::move(print));

    auto [result] = stdexec::sync_wait(std::move(work)).value();

    EXPECT_GE(result, 0);
}

TEST_CASE(stdexec_task_test)
{
    stdexec::run_loop loop;

    std::jthread worker([&](std::stop_token st)
                        {
        std::stop_callback cb{st, [&]{ loop.finish(); }};
        loop.run(); });

    auto string = "hello world"s;

    auto hello_task_fn = []() -> exec::task<int>
    {
        co_return 42;
    };

    auto print_task_fn = [](auto &&task_fn) -> exec::task<int>
    {
        auto value = co_await task_fn();
        std::printf("Value: %d\n", value);
        co_return value;
    };

    stdexec::scheduler auto io_thread = loop.get_scheduler();
    stdexec::sender auto work = stdexec::on(io_thread, std::move(print_task_fn(hello_task_fn)));

    auto [value] = stdexec::sync_wait(std::move(work)).value();
    EXPECT_EQ(value, 42);
}

TEST_CASE(stdexec_inline_scheduler_test)
{
    exec::inline_scheduler scheduler;

    auto string = "hello world"s;

    auto hello_task_fn = []() -> exec::task<int>
    {
        co_return 42;
    };

    auto print_task_fn = [](auto &&task_fn) -> exec::task<int>
    {
        auto value = co_await task_fn();
        std::printf("Value: %d\n", value);
        co_return value;
    };

    stdexec::sender auto work = stdexec::on(scheduler, std::move(print_task_fn(hello_task_fn)));

    auto [value] = stdexec::sync_wait(std::move(work)).value();
    EXPECT_EQ(value, 42);
}

TEST_CASE(stdexec_runtime_scheduler_test)
{
    webcraft::async::runtime::runtime_context context;

    std::jthread worker([&](std::stop_token st)
                        {
        std::stop_callback cb{st, [&]{ context.finish(); }};
        context.run(); });

    auto string = "hello world"s;

    stdexec::sender auto hello = stdexec::just(string);
    stdexec::sender auto print = std::move(hello) | stdexec::then([](std::string msg)
                                                                  { return std::puts(msg.c_str()); });

    stdexec::scheduler auto io_thread = context.get_scheduler();
    stdexec::sender auto work = stdexec::on(io_thread, std::move(print));

    auto [result] = stdexec::sync_wait(std::move(work)).value();

    EXPECT_GE(result, 0);
}
