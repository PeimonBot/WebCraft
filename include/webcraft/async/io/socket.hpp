#pragma once

#include "core.hpp"

namespace webcraft::async::io::socket
{
    struct connection_info
    {
        std::string host;
        std::uint16_t port;
    };

    class tcp_descriptor
    {
    private:
        std::optional<connection_info> info;

    public:
        tcp_descriptor() noexcept = default;
        tcp_descriptor(tcp_descriptor &&other) noexcept : info(std::move(other.info)) {}
        tcp_descriptor &operator=(tcp_descriptor &&other) noexcept
        {
            if (this != &other)
            {
                info = std::exchange(other.info, std::nullopt);
            }
            return *this;
        }
        virtual ~tcp_descriptor() = default;

        // common operations
        virtual void set_connection_options(const connection_info &info)
        {
            this->info = info;
        }

        std::optional<std::string> get_host() const
        {
            return info.transform([](const connection_info &info)
                                  { return info.host; });
        }

        std::optional<std::uint16_t> get_port() const
        {
            return info.transform([](const connection_info &info)
                                  { return info.port; });
        }

        virtual void close() = 0;
    };

    class tcp_client_descriptor : public tcp_descriptor
    {
    public:
        tcp_client_descriptor() noexcept : tcp_descriptor() {}
        tcp_client_descriptor(tcp_client_descriptor &&) noexcept = default;
        tcp_client_descriptor &operator=(tcp_client_descriptor &&) noexcept = default;
        virtual ~tcp_client_descriptor() = default;

        virtual task<size_t> read_bytes(std::span<char> buffer) = 0;
        virtual task<size_t> write_bytes(std::span<const char> buffer) = 0;
        virtual task<bool> connect() = 0;
    };

    std::shared_ptr<tcp_client_descriptor> make_tcp_client_descriptor(const connection_info &info);
    inline std::shared_ptr<tcp_client_descriptor> make_tcp_client_descriptor(const std::string &host, std::uint16_t port)
    {
        connection_info info{host, port};
        return make_tcp_client_descriptor(info);
    }

    class tcp_server_descriptor : public tcp_descriptor
    {
    public:
        tcp_server_descriptor() noexcept : tcp_descriptor() {}
        tcp_server_descriptor(tcp_server_descriptor &&) noexcept = default;
        tcp_server_descriptor &operator=(tcp_server_descriptor &&) noexcept = default;
        virtual ~tcp_server_descriptor() = default;

        // server related operations
        virtual task<std::shared_ptr<tcp_client_descriptor>> accept() = 0;
        virtual void listen(size_t backlog) = 0;
    };

    std::shared_ptr<tcp_server_descriptor> make_tcp_server_descriptor(const connection_info &info);
    inline std::shared_ptr<tcp_server_descriptor> make_tcp_server_descriptor(const std::string &host, std::uint16_t port)
    {
        connection_info info{host, port};
        return make_tcp_server_descriptor(info);
    }

    class tcp_readable_stream
    {
    private:
        std::shared_ptr<tcp_client_descriptor> descriptor;

    public:
        explicit tcp_readable_stream(std::shared_ptr<tcp_client_descriptor> descriptor) : descriptor(std::move(descriptor)) {}
        tcp_readable_stream(tcp_readable_stream &&other) noexcept : descriptor(std::exchange(other.descriptor, nullptr)) {}
        tcp_readable_stream &operator=(tcp_readable_stream &&other) noexcept
        {
            if (this != &other)
            {
                descriptor = std::exchange(other.descriptor, nullptr);
            }
            return *this;
        }
        tcp_readable_stream(const tcp_readable_stream &) = delete;
        tcp_readable_stream &operator=(const tcp_readable_stream &) = delete;

        task<std::optional<char>> recv()
        {
            std::array<char, 1> buffer;
            auto bytes_read = co_await recv(buffer);
            if (bytes_read == 0)
            {
                co_return std::nullopt;
            }
            co_return buffer[0];
        }

        task<size_t> recv(std::span<char> buffer)
        {
            if (!descriptor)
                throw std::runtime_error("Descriptor is not initialized");

            return descriptor->read_bytes(buffer);
        }
    };

    class tcp_writable_stream
    {
    private:
        std::shared_ptr<tcp_client_descriptor> descriptor;

    public:
        explicit tcp_writable_stream(std::shared_ptr<tcp_client_descriptor> descriptor) : descriptor(std::move(descriptor)) {}
        tcp_writable_stream(tcp_writable_stream &&other) noexcept : descriptor(std::exchange(other.descriptor, nullptr)) {}
        tcp_writable_stream &operator=(tcp_writable_stream &&other) noexcept
        {
            if (this != &other)
            {
                descriptor = std::exchange(other.descriptor, nullptr);
            }
            return *this;
        }
        tcp_writable_stream(const tcp_writable_stream &) = delete;
        tcp_writable_stream &operator=(const tcp_writable_stream &) = delete;

        task<bool> send(char c)
        {
            std::array<char, 1> buffer = {c};
            size_t sent = co_await send(buffer);
            co_return sent == 1;
        }

        task<size_t> send(std::span<const char> buffer)
        {
            if (!descriptor)
                throw std::runtime_error("Descriptor is not initialized");

            return descriptor->write_bytes(buffer);
        }
    };

    static_assert(async_buffered_readable_stream<tcp_readable_stream, char>, "tcp_readable_stream should be an async buffered readable stream");
    static_assert(async_buffered_writable_stream<tcp_writable_stream, char>, "tcp_writable_stream should be an async buffered writable stream");

    class tcp_socket
    {
    private:
        std::shared_ptr<tcp_client_descriptor> descriptor;
        std::unique_ptr<tcp_readable_stream> readable_stream;
        std::unique_ptr<tcp_writable_stream> writable_stream;

    public:
        tcp_socket(std::shared_ptr<tcp_client_descriptor> descriptor) noexcept : descriptor(std::move(descriptor)) {}
        tcp_socket(tcp_socket &&other) noexcept : descriptor(std::exchange(other.descriptor, nullptr)),
                                                  readable_stream(std::exchange(other.readable_stream, nullptr)),
                                                  writable_stream(std::exchange(other.writable_stream, nullptr)) {}
        tcp_socket &operator=(tcp_socket &&other) noexcept
        {
            if (this != &other)
            {
                descriptor = std::exchange(other.descriptor, nullptr);
            }
            return *this;
        }
        tcp_socket(const tcp_socket &) = delete;
        tcp_socket &operator=(const tcp_socket &) = delete;

        virtual ~tcp_socket()
        {
            if (descriptor)
                descriptor->close();
        }

        task<void> connect()
        {
            if (!descriptor)
                throw std::runtime_error("Socket descriptor is not initialized");

            if (co_await descriptor->connect())
            {
                readable_stream = std::make_unique<tcp_readable_stream>(descriptor);
                writable_stream = std::make_unique<tcp_writable_stream>(descriptor);
                co_return;
            }
            else
            {
                throw std::runtime_error("Failed to connect to the server");
            }
        }

        tcp_readable_stream &get_readable_stream() const
        {
            if (!readable_stream)
                throw std::runtime_error("Socket is not connected");

            return *readable_stream;
        }

        tcp_writable_stream &get_writable_stream() const
        {
            if (!writable_stream)
                throw std::runtime_error("Socket is not connected");

            return *writable_stream;
        }
    };

    class tcp_listener
    {
    private:
        std::shared_ptr<tcp_server_descriptor> descriptor;

    public:
        tcp_listener(std::shared_ptr<tcp_server_descriptor> descriptor) : descriptor(std::move(descriptor)) {}
        tcp_listener(tcp_listener &&other) noexcept : descriptor(std::exchange(other.descriptor, nullptr)) {}
        tcp_listener &operator=(tcp_listener &&other) noexcept
        {
            if (this != &other)
            {
                descriptor = std::exchange(other.descriptor, nullptr);
            }
            return *this;
        }
        tcp_listener(const tcp_listener &) = delete;
        tcp_listener &operator=(const tcp_listener &) = delete;

        virtual ~tcp_listener()
        {
            if (descriptor)
                descriptor->close();
        }

        void listen(size_t backlog)
        {
            if (!descriptor)
                throw std::runtime_error("Listener descriptor is not initialized");

            descriptor->listen(backlog);
        }

        task<tcp_socket> accept()
        {
            if (!descriptor)
                throw std::runtime_error("Listener descriptor is not initialized");

            auto client_descriptor = co_await descriptor->accept();
            if (client_descriptor)
            {
                co_return {std::move(client_descriptor)};
            }
            else
            {
                throw std::runtime_error("Failed to accept connection");
            }
        }
    };

    inline tcp_socket make_tcp_socket(const connection_info &info)
    {
        auto descriptor = make_tcp_client_descriptor(info);
        if (!descriptor)
            throw std::runtime_error("Failed to create TCP client descriptor");

        return tcp_socket(std::move(descriptor));
    }

    inline tcp_socket make_tcp_socket(const std::string &host, std::uint16_t port)
    {
        connection_info info{host, port};
        return make_tcp_socket(info);
    }

    inline tcp_listener make_tcp_listener(const connection_info &info)
    {
        auto descriptor = make_tcp_server_descriptor(info);
        if (!descriptor)
            throw std::runtime_error("Failed to create TCP server descriptor");

        return tcp_listener(std::move(descriptor));
    }

    inline tcp_listener make_tcp_listener(const std::string &host, std::uint16_t port)
    {
        connection_info info{host, port};
        return make_tcp_listener(info);
    }
}