#pragma once

#include <webcraft/async/runtime.hpp>

namespace webcraft::async::detail
{
    using runtime_event_cancellation_callback = std::function<void()>;
    using runtime_event_async_delegate = std::function<void(runtime_event *)>;

    static std::function<void()> make_noop()
    {
        return [] {};
    }

    inline auto create_runtime_event(runtime_event_async_delegate start_fn, runtime_event_cancellation_callback cancellation_callback = make_noop(), std::stop_token token = get_stop_token())
    {
        struct runtime_event_impl : public runtime_event
        {
        private:
            runtime_event_async_delegate start_fn;
            runtime_event_cancellation_callback callback;

        public:
            runtime_event_impl(runtime_event_async_delegate start_fn, runtime_event_cancellation_callback callback, std::stop_token token)
                : runtime_event(token), start_fn(std::move(start_fn)), callback(std::move(callback))
            {
            }

            ~runtime_event_impl() = default;

            void try_native_cancel() override
            {
                if (callback)
                {
                    callback();
                }
            }

            void try_start() override
            {
                runtime_event *ev = this;
                if (start_fn)
                {
                    start_fn(ev);
                }
            }
        };

        return std::make_unique<runtime_event_impl>(std::move(start_fn), std::move(cancellation_callback), token);
    }
}