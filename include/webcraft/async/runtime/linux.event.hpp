#pragma once

#ifdef __linux__

#include <webcraft/async/runtime.hpp>
#include <liburing.h>

namespace webcraft::async::detail::linux
{

    struct io_uring_runtime_event : public webcraft::async::detail::runtime_event
    {
        std::mutex &io_uring_mutex;
        struct io_uring *global_ring;

        io_uring_runtime_event(std::stop_token token)
            : webcraft::async::detail::runtime_event(token)
        {
            io_uring_mutex = webcraft::async::detail::get_runtime_mutex();
            global_ring = reinterpret_cast<io_uring *>(webcraft::async::detail::get_native_handle());
        }

        void try_native_cancel() override
        {
            std::lock_guard<std::mutex> lock(io_uring_mutex);
            struct io_uring_sqe *sqe = io_uring_get_sqe(&global_ring);
            if (!sqe)
            {
                throw std::runtime_error("Failed to get SQE from io_uring");
            }
            io_uring_prep_cancel64(sqe, reinterpret_cast<uint64_t>(this), IORING_ASYNC_CANCEL_USERDATA);
            int ret = io_uring_submit(&global_ring);
            if (ret < 0)
            {
                throw std::runtime_error("Failed to submit SQE to io_uring: " + std::string(std::strerror(-ret)));
            }
        }

        void try_start() override
        {
            std::lock_guard<std::mutex> lock(io_uring_mutex);
            struct io_uring_sqe *sqe = io_uring_get_sqe(&global_ring);
            if (!sqe)
            {
                throw std::runtime_error("Failed to get SQE from io_uring");
            }

            perform_io_uring_operation(sqe);

            io_uring_sqe_set_data64(sqe, reinterpret_cast<uint64_t>(this));
            int ret = io_uring_submit(&global_ring);
            if (ret < 0)
            {
                throw std::runtime_error("Failed to submit SQE to io_uring: " + std::string(std::strerror(-ret)));
            }
        }

        virtual void perform_io_uring_operation(struct io_uring_sqe *sqe) = 0;
    };
}

#endif