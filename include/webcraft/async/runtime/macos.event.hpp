#pragma once

#ifdef __APPLE__

#include <webcraft/async/runtime.hpp>
#include <sys/event.h>
#include <unistd.h>

namespace webcraft::async::detail::macos
{

    struct kqueue_runtime_event : public webcraft::async::detail::runtime_event
    {
        int kq;
        struct kevent kev;

        kqueue_runtime_event(std::stop_token token = webcraft::async::get_stop_token())
            : webcraft::async::detail::runtime_event(token)
        {
            kq = webcraft::async::detail::get_native_handle();
        }

        ~kqueue_runtime_event()
        {
        }

        void try_native_cancel() override
        {
            EV_SET(&kev, event.ident, event.filter, EV_DELETE, 0, 0, nullptr);
            int ret = kevent(kq, &kev, 1, nullptr, 0, nullptr);
            if (ret < 0)
            {
                throw std::runtime_error("Failed to cancel kqueue event: " + std::string(std::strerror(errno)));
            }
        }

        void try_start() override
        {
            setup_kevent(kq, &kev);
            int ret = kevent(kq, &kev, 1, nullptr, 0, nullptr);
            if (ret < 0)
            {
                throw std::runtime_error("Failed to post kqueue event: " + std::string(std::strerror(errno)));
            }
        }

        virtual void setup_kevent(int queue, struct kevent *ev) = 0;
    };
}

#endif