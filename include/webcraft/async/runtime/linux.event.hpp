#pragma once

///////////////////////////////////////////////////////////////////////////////
// Copyright (c) Aditya Rao
// Licenced under MIT license. See LICENSE.txt for details.
///////////////////////////////////////////////////////////////////////////////

#ifdef __linux__

#include <webcraft/async/runtime.hpp>
#include <liburing.h>
#include <exception>

namespace webcraft::async::detail::linux
{

    class io_uring_runtime_error : public std::exception
    {
    public:
        explicit io_uring_runtime_error(std::string message) : msg_(message) {}
        virtual const char *what() const noexcept override
        {
            return msg_.c_str();
        }

    private:
        std::string msg_;
    };

    struct io_uring_runtime_event : public webcraft::async::detail::runtime_event
    {
        io_uring_runtime_event(std::stop_token token)
            : webcraft::async::detail::runtime_event(token)
        {
        }

        void try_native_cancel() override
        {
            auto userdata = get_user_data();

            auto func = [userdata](struct io_uring_sqe *sqe)
            {
                ::io_uring_prep_cancel64(sqe, userdata, IORING_ASYNC_CANCEL_USERDATA);
            };
            webcraft::async::detail::submit_runtime_operation(func);
        }

        void try_start() override
        {

            auto func = [this](struct io_uring_sqe *sqe)
            {
                perform_io_uring_operation(sqe);

                ::io_uring_sqe_set_data64(sqe, get_user_data());
            };

            webcraft::async::detail::submit_runtime_operation(func);
        }

        uint64_t get_user_data() const
        {
            return reinterpret_cast<uint64_t>((webcraft::async::detail::runtime_callback *)this);
        }

        virtual void perform_io_uring_operation(struct io_uring_sqe *sqe) = 0;
    };

    inline auto create_io_uring_event(webcraft::async::detail::io_uring_operation op, std::stop_token token = get_stop_token())
    {
        struct io_uring_runtime_event_impl : public io_uring_runtime_event
        {
            io_uring_runtime_event_impl(webcraft::async::detail::io_uring_operation op, std::stop_token token)
                : io_uring_runtime_event(token), operation(std::move(op))
            {
            }

            void perform_io_uring_operation(struct ::io_uring_sqe *sqe) override
            {
                operation(sqe);
            }

        private:
            io_uring_operation operation;
        };

        return std::make_unique<io_uring_runtime_event_impl>(std::move(op), token);
    }
}
#endif