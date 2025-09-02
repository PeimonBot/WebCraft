#pragma once

#include "core.hpp"
#include <webcraft/async/fire_and_forget_task.hpp>

namespace webcraft::async::io::socket
{
    struct connection_info
    {
        std::string host;
        uint16_t port;
    };

    enum class socket_stream_mode
    {
        READ,
        WRITE
    };

    namespace detail
    {

        class tcp_descriptor_base
        {
        public:
            tcp_descriptor_base() = default;
            virtual ~tcp_descriptor_base() = default;

            virtual task<void> close() = 0; // Close the socket
        };

        class tcp_socket_descriptor : public tcp_descriptor_base
        {
        public:
            tcp_socket_descriptor() = default;
            virtual ~tcp_socket_descriptor() = default;

            virtual task<void> connect(const connection_info &info) = 0;  // Connect to a server
            virtual task<size_t> read(std::span<char> buffer) = 0;        // Read data from the socket
            virtual task<size_t> write(std::span<const char> buffer) = 0; // Write data to the socket
            virtual void shutdown(socket_stream_mode mode) = 0;           // Shutdown the socket

            virtual std::string get_remote_host() = 0;
            virtual uint16_t get_remote_port() = 0;
        };

        class tcp_listener_descriptor : public tcp_descriptor_base
        {
        public:
            tcp_listener_descriptor() = default;
            virtual ~tcp_listener_descriptor() = default;

            virtual void bind(const connection_info &info) = 0;                // Bind the listener to an address
            virtual void listen(int backlog) = 0;                              // Start listening for incoming connections
            virtual task<std::shared_ptr<tcp_socket_descriptor>> accept() = 0; // Accept a new connection
        };

        std::shared_ptr<tcp_socket_descriptor> make_tcp_socket_descriptor();
        std::shared_ptr<tcp_listener_descriptor> make_tcp_listener_descriptor();

    }

    class tcp_rstream
    {
    private:
        std::shared_ptr<detail::tcp_socket_descriptor> descriptor;

    public:
        explicit tcp_rstream(std::shared_ptr<detail::tcp_socket_descriptor> desc) : descriptor(desc) {}
        ~tcp_rstream() = default;
        tcp_rstream(tcp_rstream &) = delete;
        tcp_rstream &operator=(tcp_rstream &) = delete;
        tcp_rstream(tcp_rstream &&other) : descriptor(std::exchange(other.descriptor, nullptr)) {}
        tcp_rstream &operator=(tcp_rstream &&other)
        {
            if (this != &other)
            {
                descriptor = std::exchange(other.descriptor, nullptr);
            }
            return *this;
        }

        task<size_t> recv(std::span<char> buffer)
        {
            return descriptor->read(buffer);
        }

        task<std::optional<char>> recv()
        {
            std::array<char, 1> buf;
            if (co_await recv(buf))
            {
                co_return buf[0];
            }
            co_return std::nullopt;
        }

        task<void> close()
        {
            descriptor->shutdown(socket_stream_mode::READ);
            co_return;
        }
    };

    static_assert(async_readable_stream<tcp_rstream, char>);
    static_assert(async_buffered_readable_stream<tcp_rstream, char>);
    static_assert(async_closeable_stream<tcp_rstream, char>);

    class tcp_wstream
    {
    private:
        std::shared_ptr<detail::tcp_socket_descriptor> descriptor;

    public:
        explicit tcp_wstream(std::shared_ptr<detail::tcp_socket_descriptor> desc) : descriptor(desc) {}
        ~tcp_wstream() = default;
        tcp_wstream(tcp_wstream &) = delete;
        tcp_wstream &operator=(tcp_wstream &) = delete;
        tcp_wstream(tcp_wstream &&other) : descriptor(std::exchange(other.descriptor, nullptr)) {}
        tcp_wstream &operator=(tcp_wstream &&other)
        {
            if (this != &other)
            {
                descriptor = std::exchange(other.descriptor, nullptr);
            }
            return *this;
        }

        task<size_t> send(std::span<const char> buffer)
        {
            return descriptor->write(buffer);
        }

        task<bool> send(char b)
        {
            std::array<char, 1> buf;
            buf[0] = b;
            if (co_await send(buf))
            {
                co_return true;
            }
            co_return false;
        }

        task<void> close()
        {
            descriptor->shutdown(socket_stream_mode::WRITE);
            co_return;
        }
    };

    static_assert(async_writable_stream<tcp_wstream, char>);
    static_assert(async_buffered_writable_stream<tcp_wstream, char>);
    static_assert(async_closeable_stream<tcp_wstream, char>);

    class tcp_socket
    {
    private:
        std::shared_ptr<detail::tcp_socket_descriptor> descriptor;
        tcp_rstream read_stream;
        tcp_wstream write_stream;
        bool read_shutdown{false};
        bool write_shutdown{false};

    public:
        tcp_socket(std::shared_ptr<detail::tcp_socket_descriptor> desc) : descriptor(desc), read_stream(descriptor), write_stream(descriptor)
        {
        }

        ~tcp_socket()
        {
            fire_and_forget(close());
        }

        tcp_socket(tcp_socket &&other) noexcept
            : descriptor(std::exchange(other.descriptor, nullptr)),
              read_stream(std::move(other.read_stream)),
              write_stream(std::move(other.write_stream))
        {
        }

        tcp_socket &operator=(tcp_socket &&other) noexcept
        {
            if (this != &other)
            {
                descriptor = std::exchange(other.descriptor, nullptr);
                read_stream = std::move(other.read_stream);
                write_stream = std::move(other.write_stream);
            }
            return *this;
        }

        task<void> connect(const connection_info &info)
        {
            if (!descriptor)
                throw std::runtime_error("Descriptor is null");

            co_await descriptor->connect(info);
        }

        tcp_rstream &get_readable_stream()
        {
            if (!descriptor)
                throw std::runtime_error("Descriptor is null");
            return read_stream;
        }

        tcp_wstream &get_writable_stream()
        {
            if (!descriptor)
                throw std::runtime_error("Descriptor is null");
            return write_stream;
        }

        void shutdown_channel(socket_stream_mode mode)
        {
            if (mode == socket_stream_mode::READ && !read_shutdown)
            {
                descriptor->shutdown(socket_stream_mode::READ);
                this->read_shutdown = true;
            }
            else if (mode == socket_stream_mode::WRITE && !write_shutdown)
            {
                descriptor->shutdown(socket_stream_mode::WRITE);
                this->write_shutdown = true;
            }
        }

        task<void> close()
        {
            if (descriptor)
            {
                shutdown_channel(socket_stream_mode::READ);
                shutdown_channel(socket_stream_mode::WRITE);
                co_await descriptor->close();
                descriptor.reset();
            }
        }

        inline std::string get_remote_host()
        {
            return descriptor->get_remote_host();
        }

        inline uint16_t get_remote_port()
        {
            return descriptor->get_remote_port();
        }
    };

    class tcp_listener
    {
    private:
        std::shared_ptr<detail::tcp_listener_descriptor> descriptor;

    public:
        tcp_listener(std::shared_ptr<detail::tcp_listener_descriptor> desc) : descriptor(std::move(desc)) {}
        ~tcp_listener()
        {
            if (descriptor)
            {
                fire_and_forget(descriptor->close());
            }
        }

        void bind(const connection_info &info)
        {
            descriptor->bind(info);
        }

        void listen(int backlog)
        {
            descriptor->listen(backlog);
        }

        task<tcp_socket> accept()
        {
            co_return co_await descriptor->accept();
        }
    };

    inline tcp_socket make_tcp_socket()
    {
        return tcp_socket(detail::make_tcp_socket_descriptor());
    }

    inline tcp_listener make_tcp_listener()
    {
        return tcp_listener(detail::make_tcp_listener_descriptor());
    }
}