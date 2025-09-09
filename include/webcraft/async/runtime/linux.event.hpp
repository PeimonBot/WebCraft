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

    struct io_uring_runtime_event : public webcraft::async::detail::runtime_event
    {
        std::mutex &io_uring_mutex;
        struct io_uring *global_ring;

        io_uring_runtime_event(std::stop_token token)
            : webcraft::async::detail::runtime_event(token), io_uring_mutex(webcraft::async::detail::get_runtime_mutex())
        {
            global_ring = reinterpret_cast<io_uring *>(webcraft::async::detail::get_native_handle());
        }

        void try_native_cancel() override
        {
            std::lock_guard<std::mutex> lock(io_uring_mutex);
            struct io_uring_sqe *sqe = io_uring_get_sqe(global_ring);
            if (!sqe)
            {
                throw std::runtime_error("Failed to get SQE from io_uring");
            }
            io_uring_prep_cancel64(sqe, get_user_data(), IORING_ASYNC_CANCEL_USERDATA);
            int ret = io_uring_submit(global_ring);
            if (ret < 0)
            {
                throw std::runtime_error("Failed to submit SQE to io_uring: " + std::to_string(-ret));
            }
        }

        void try_start() override
        {
            std::lock_guard<std::mutex> lock(io_uring_mutex);
            struct io_uring_sqe *sqe = io_uring_get_sqe(global_ring);
            if (!sqe)
            {
                throw std::runtime_error("Failed to get SQE from io_uring");
            }

            perform_io_uring_operation(sqe);

            io_uring_sqe_set_data64(sqe, get_user_data());
            int ret = io_uring_submit(global_ring);
            if (ret < 0)
            {
                throw std::runtime_error("Failed to submit SQE to io_uring: " + std::to_string(-ret));
            }
        }

        uint64_t get_user_data() const
        {
            return reinterpret_cast<uint64_t>((webcraft::async::detail::runtime_callback *)this);
        }

        virtual void perform_io_uring_operation(struct io_uring_sqe *sqe) = 0;
    };

    using io_uring_operation = std::function<void(struct io_uring_sqe *)>;

    inline auto create_io_uring_event(io_uring_operation op, std::stop_token token = get_stop_token())
    {
        struct io_uring_runtime_event_impl : public io_uring_runtime_event
        {
            io_uring_runtime_event_impl(io_uring_operation op, std::stop_token token)
                : io_uring_runtime_event(token), operation(std::move(op))
            {
            }

            void perform_io_uring_operation(struct io_uring_sqe *sqe) override
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