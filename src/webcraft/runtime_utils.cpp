#include <webcraft/async/runtime.hpp>
#include <webcraft/async/executors.hpp>
#include <webcraft/async/timer_service.hpp>

// basic executor implementation, which is a simple executor that yields control back to the runtime
class basic_executor : public webcraft::async::executor
{
private:
    webcraft::async::async_runtime &runtime;

public:
    basic_executor(webcraft::async::async_runtime &runtime) : runtime(runtime)
    {
    }

    webcraft::async::task<void> schedule(webcraft::async::scheduling_priority priority = webcraft::async::scheduling_priority::LOW) override
    {
        return runtime.yield();
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