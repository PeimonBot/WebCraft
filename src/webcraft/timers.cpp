#include <webcraft/async/runtime.hpp>
#include <webcraft/async/timer_service.hpp>

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