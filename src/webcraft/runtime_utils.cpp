#include <webcraft/async/runtime.hpp>
#include <webcraft/async/executors.hpp>
#include <webcraft/async/timer_service.hpp>
#include <thread>

// basic executor implementation, which is a simple executor that yields control back to the runtime
class basic_executor : public webcraft::async::executor
{
private:
    webcraft::async::async_runtime &runtime;

public:
    basic_executor(webcraft::async::async_runtime &runtime) : runtime(runtime)
    {
    }

    webcraft::async::task<void> schedule(webcraft::async::scheduling_priority priority) override
    {
        return runtime.yield();
    }
};

// thread per task executor, highly inefficient but shows the idea of how we can combine both concurrency and parallelism
class thread_per_task : public webcraft::async::executor
{
public:
    thread_per_task() {}

    webcraft::async::task<void> schedule(webcraft::async::scheduling_priority priority) override
    {
        struct thread_per_task_awaitable
        {
            bool await_ready() { return false; }
            void await_suspend(std::coroutine_handle<> h)
            {
                std::jthread thread([h]()
                                    { h.resume(); });
            }
            void await_resume() {}
        };

        co_await thread_per_task_awaitable{};
        co_return;
    }
};

class fixed_size_tpl : public webcraft::async::executor
{
public:
    fixed_size_tpl(size_t threads) {}

    webcraft::async::task<void> schedule(webcraft::async::scheduling_priority priority) override
    {
        
    }
};

webcraft::async::executor_service::executor_service(async_runtime &runtime, executor_service_params &params) : runtime(runtime)
{
    // TODO: implement the executor service strategy based on the params
    this->strategy = std::make_unique<basic_executor>(runtime);
}

// TODO: implement these
webcraft::async::executor_service::~executor_service()
{
}

webcraft::async::timer_service::timer_service(async_runtime &runtime) : runtime(runtime)
{
}

webcraft::async::timer_service::~timer_service()
{
}

webcraft::async::task<void> webcraft::async::timer_service::sleep_for(std::chrono::steady_clock::duration duration, std::stop_token token)
{
    // TODO: have not implemented this yet
    return runtime.yield();
}