
#define TEST_SUITE_NAME STDEXECTestSuite

#include "test_suite.hpp"

#include <stdexec/execution.hpp>
#include <cstdio>
#include <string>
#include <thread>
#include <utility>
#include <exec/task.hpp>

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
