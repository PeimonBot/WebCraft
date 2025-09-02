#pragma once

#ifdef __APPLE__

#include <atomic>
#include <webcraft/async/runtime.hpp>
#include <sys/event.h>
#include <unistd.h>

namespace webcraft::async::detail::macos
{

    struct kqueue_runtime_event : public webcraft::async::detail::runtime_event
    {
    private:
        bool cancelled{false};
        struct kevent event;
        int queue;

    public:
        kqueue_runtime_event(std::stop_token token) : runtime_event(token)
        {
            queue = (int)webcraft::async::detail::get_native_handle();
        }

        ~kqueue_runtime_event()
        {
            if (!cancelled)
                try_native_cancel();
        }

        void try_native_cancel() override
        {
            // remove yield event listener
            EV_SET(&event, event.ident, event.filter, EV_DELETE, 0, 0, nullptr);
            int result = kevent(queue, &event, 1, nullptr, 0, nullptr);
            if (result < 0)
            {
                throw std::runtime_error("Failed to remove event from kqueue: " + std::to_string(result));
            }
            cancelled = true;
        }

        void try_start() override
        {
            // listen to the yield event
            void *data = this;

            prep_event(&event, data);
        }

        virtual void prep_event(struct kevent *event, void *data) = 0;
    };

    using kqueue_operation = std::function<void(struct kevent *, void *)>;

    inline auto create_kqueue_event(kqueue_operation op, std::stop_token token = webcraft::async::get_stop_token())
    {
        struct kqueue_runtime_event_impl : public kqueue_runtime_event
        {
            kqueue_runtime_event_impl(kqueue_operation op, std::stop_token token) : op(op), kqueue_runtime_event(token) {}

            void prep_event(struct kevent *event, void *data) override
            {
                op(event, data);
            }

        private:
            kqueue_operation op;
        };

        return std::make_unique<kqueue_runtime_event_impl>(op, token);
    }
}

#endif