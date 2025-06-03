#pragma once

#include <webcraft/async/runtime.hpp>

namespace webcraft::async::io
{
    class io_service final
    {
    private:
        webcraft::async::async_runtime &runtime;

    public:
        explicit io_service(webcraft::async::async_runtime &runtime) : runtime(runtime) {}
    };
}