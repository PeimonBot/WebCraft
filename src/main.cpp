#include <thread>
#include <chrono>
#include <webcraft/async/awaitable.hpp>
#include <iostream>

webcraft::async::task<void> example_task()
{
    std::cout << "Running example task..." << std::endl;
    co_return;
}

int main()
{
    auto t = example_task();
}