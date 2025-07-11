#pragma once

#include <coroutine>
#include <concepts>
#include <exception>
#include <memory>
#include <iostream>
#include <stdexec/execution.hpp>
#include <stop_token>

#include <exec/inline_scheduler.hpp>
#include <exec/timed_scheduler.hpp>

namespace webcraft::async::runtime
{
    // Generic runtime event
    class runtime_event
    {
    private:
        int result;

    public:
        virtual void execute_callback() = 0;

        int get_result() const
        {
            return result;
        }

        void set_result(int res)
        {
            result = res;
        }
    };

    /// @brief Concept for runtime context traits.
    template <typename T>
    concept runtime_context_trait = requires(T context) {
        { context.get_scheduler() };
        { context.get_native_handle() };
        { context.run() } -> void;
        { context.finish() } -> void;
    } && exec::timed_scheduler<decltype(std::declval<T>().get_scheduler())>;

    /// @brief
    /// @tparam T
    template <runtime_context_trait T>
    using native_runtime_handle = decltype(std::declval<T>().get_native_handle());

    // forward declare the runtime context
    struct runtime_context;

    namespace fs
    {
        namespace internal
        {
            struct file_handle
            {

                file_handle() = default;
                virtual ~file_handle() = default;

                static std::unique_ptr<file_handle> create(const char *path, file_mode mode);
            };

            enum class file_mode
            {
                read,
                write,
                append,
                read_write
            };

            stdexec::sender auto read_file_async(file_handle *handle, void *buffer, size_t size) noexcept;
            stdexec::sender auto write_file_async(file_handle *handle, const void *buffer, size_t size) noexcept;
        }
    }

    namespace tcp
    {

        namespace internal
        {
            struct socket_handle
            {
            };

            struct socket_address
            {
                std::string host;
                std::string port;
            };

            std::unique_ptr<socket_handle> create_socket();
            void bind(socket_handle *handle, const socket_address &address) noexcept;
            void listen(socket_handle *handle, int backlog) noexcept;
            stdexec::sender auto connect_async(socket_handle *handle, const socket_address &address) noexcept;
            stdexec::sender auto accept_async(socket_handle *handle) noexcept;
            stdexec::sender auto send_async(socket_handle *handle, const void *buffer, size_t size) noexcept;
            stdexec::sender auto receive_async(socket_handle *handle, void *buffer, size_t size) noexcept;
            stdexec::sender auto wait_for_readable(socket_handle *handle) noexcept;
            stdexec::sender auto wait_for_writable(socket_handle *handle) noexcept;
        }
    }
}