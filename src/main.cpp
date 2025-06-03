#include <thread>
#include <chrono>
#include <iostream>
#include <webcraft/async/runtime.hpp>

using namespace webcraft::async;

task<void> most_likely_incomplete()
{
    for (int i = 0; i < 10000; i++)
    {
        std::cout << "This is a task that is most likely not gonna complete. Count: " << i << std::endl;
        co_await async_runtime::get_instance().yield();
    }
}

task<void> example_task()
{
    auto &runtime = async_runtime::get_instance();
    std::cout << "Running example task..." << std::endl;
    most_likely_incomplete();

    co_await runtime.yield();

    int count = 0;

    auto &dispatcher = runtime.get_executor_service();
    auto &timer = runtime.get_timer_service();

    for (int i = 0; i < 5; i++)
    {
        count = co_await value_of(i);
        co_await timer.sleep_for(std::chrono::seconds(1));

        std::cout << "Count: " << count << std::endl;
        co_await dispatcher.schedule();
    }

    std::cout << "Example task completed." << std::endl;
    co_return;
}

int main()
{
    auto &runtime = async_runtime::get_instance();
    runtime.run_async(example_task);
}