#include <webcraft/async/io/socket.hpp>

using webcraft::async::io::socket::connection_info;
using namespace webcraft::async;

#ifdef __linux__
#include <webcraft/async/runtime/linux.event.hpp>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

struct io_uring_tcp_connect : public webcraft::async::detail::linux::io_uring_runtime_event
{
    int fd;
    connection_info conn_info;

    io_uring_tcp_connect(int fd, const connection_info &info, std::stop_token token)
        : webcraft::async::detail::linux::io_uring_runtime_event(token), fd(fd), conn_info(info)
    {
    }

    void perform_io_uring_operation(struct io_uring_sqe *sqe) override
    {
        struct sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(conn_info.port);
        inet_pton(AF_INET, conn_info.host.c_str(), &addr.sin_addr);

        io_uring_prep_connect(sqe, fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr));
    }
};

struct io_uring_tcp_read : public webcraft::async::detail::linux::io_uring_runtime_event
{
    int fd;
    std::span<char> buffer;

    io_uring_tcp_read(int fd, std::span<char> buffer, std::stop_token token)
        : webcraft::async::detail::linux::io_uring_runtime_event(token), fd(fd), buffer(buffer)
    {
    }

    void perform_io_uring_operation(struct io_uring_sqe *sqe) override
    {
        io_uring_prep_read(sqe, fd, buffer.data(), buffer.size(), 0);
    }
};

struct io_uring_tcp_write : public webcraft::async::detail::linux::io_uring_runtime_event
{
    int fd;
    std::span<const char> buffer;

    io_uring_tcp_write(int fd, std::span<const char> buffer, std::stop_token token)
        : webcraft::async::detail::linux::io_uring_runtime_event(token), fd(fd), buffer(buffer)
    {
    }

    void perform_io_uring_operation(struct io_uring_sqe *sqe) override
    {
        io_uring_prep_write(sqe, fd, buffer.data(), buffer.size(), 0);
    }
};

struct io_uring_tcp_client_descriptor : public webcraft::async::io::socket::tcp_client_descriptor
{
    int fd;
    connection_info conn_info;

    io_uring_tcp_client_descriptor(const connection_info &info)
    {
        set_connection_options(info);
        // Here you would normally create a real TCP client descriptor.
        // For this example, we just initialize the fd and conn_info.
        fd = -1; // Placeholder for actual file descriptor
        conn_info = info;
    }

    task<size_t> read_bytes(std::span<char> buffer)
    {
        if (fd < 0)
        {
            throw std::runtime_error("Failed to create socket");
        }

        auto ev = as_awaitable(std::make_unique<io_uring_tcp_read>(fd, buffer, std::stop_token{}));
        co_await ev;
        if (ev.get_result() < 0)
        {
            throw std::runtime_error("Failed to read from socket: " + std::string(std::strerror(-ev.get_result())));
        }
        co_return ev.get_result();
    }

    task<size_t> write_bytes(std::span<const char> buffer)
    {
        if (fd < 0)
        {
            throw std::runtime_error("Failed to create socket");
        }
        auto ev = as_awaitable(std::make_unique<io_uring_tcp_write>(fd, buffer, std::stop_token{}));
        co_await ev;
        if (ev.get_result() < 0)
        {
            throw std::runtime_error("Failed to write to socket: " + std::string(std::strerror(-ev.get_result())));
        }
        co_return ev.get_result();
    }

    task<bool> connect()
    {
        fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0)
        {
            throw std::runtime_error("Failed to create socket");
        }

        auto ev = as_awaitable(std::make_unique<io_uring_tcp_connect>(fd, conn_info, std::stop_token{}));
        co_await ev;
        if (ev.get_result() != 0)
        {
            throw std::runtime_error("Failed to connect to server: " + std::string(std::strerror(-ev.get_result())));
        }

        co_return true;
    }

    void close()
    {
        ::close(fd);
    }
};

struct io_uring_tcp_server_descriptor : public webcraft::async::io::socket::tcp_server_descriptor
{
    int fd;
    connection_info conn_info;

    explicit io_uring_tcp_server_descriptor(const connection_info &info)
    {
        set_connection_options(info);
        // Here you would normally create a real TCP server descriptor.
        // For this example, we just initialize the fd and conn_info.
        fd = -1; // Placeholder for actual file descriptor
        conn_info = info;
    }

    task<std::shared_ptr<tcp_client_descriptor>> accept()
    {
    }

    void listen(size_t backlog)
    {
    }

    void close()
    {
    }
};

std::shared_ptr<webcraft::async::io::socket::tcp_client_descriptor> webcraft::async::io::socket::make_tcp_client_descriptor(const connection_info &info)
{
    // Here you would normally create a real TCP client descriptor.
    // For this example, we return a mock descriptor.
    return std::make_shared<io_uring_tcp_client_descriptor>(info);
}

std::shared_ptr<webcraft::async::io::socket::tcp_server_descriptor> webcraft::async::io::socket::make_tcp_server_descriptor(const connection_info &info)
{
    // Here you would normally create a real TCP server descriptor.
    // For this example, we return a mock descriptor.
    return std::make_shared<io_uring_tcp_server_descriptor>(info);
}

#elif defined(_WIN32)
#elif defined(__APPLE__)
#else

struct mock_tcp_client_descriptor : public webcraft::async::io::socket::tcp_client_descriptor
{
    mock_tcp_client_descriptor(const webcraft::async::io::socket::connection_info &info)
    {
        set_connection_options(info);
        std::cout << "Mock TCP client descriptor created for host: " << info.host << ", port: " << info.port << std::endl;
    }

    ~mock_tcp_client_descriptor() override
    {
        close();
        std::cout << "Mock TCP client descriptor destroyed." << std::endl;
    }

    task<size_t> read_bytes(std::span<char> buffer) override
    {
        std::cout << "Mock read_bytes called with buffer size: " << buffer.size() << std::endl;
        co_return 0; // Simulate no bytes read
    }

    task<size_t> write_bytes(std::span<const char> buffer) override
    {
        std::cout << "Mock write_bytes called with buffer size: " << buffer.size() << std::endl;
        // Mock implementation for testing purposes
        co_return buffer.size(); // Simulate writing all bytes
    }

    task<bool> connect() override
    {
        std::cout << "Mock connect called." << std::endl;
        co_return true; // Simulate successful connection
    }

    void close() override
    {
        std::cout << "Mock close called." << std::endl;
        // Simulate closing the connection
    }
};

struct mock_tcp_server_descriptor : public webcraft::async::io::socket::tcp_server_descriptor
{
    mock_tcp_server_descriptor(const webcraft::async::io::socket::connection_info &info)
    {
        set_connection_options(info);
    }

    ~mock_tcp_server_descriptor() override
    {
        close();
        std::cout << "Mock TCP server descriptor destroyed." << std::endl;
    }

    task<std::shared_ptr<webcraft::async::io::socket::tcp_client_descriptor>> accept() override
    {
        std::cout << "Mock accept called." << std::endl;
        // Simulate accepting a client connection
        co_return std::make_shared<mock_tcp_client_descriptor>(connection_info{"localhost", 8080});
    }

    void listen(size_t backlog) override
    {
        std::cout << "Mock listen called with backlog: " << backlog << std::endl;
        // Simulate listening on the server socket
    }

    void close() override
    {
        std::cout << "Mock server descriptor closed." << std::endl;
        // Simulate closing the server socket
    }
};

std::shared_ptr<webcraft::async::io::socket::tcp_client_descriptor> webcraft::async::io::socket::make_tcp_client_descriptor(const connection_info &info)
{
    // Here you would normally create a real TCP client descriptor.
    // For this example, we return a mock descriptor.
    return std::make_shared<mock_tcp_client_descriptor>(info);
}

std::shared_ptr<webcraft::async::io::socket::tcp_server_descriptor> webcraft::async::io::socket::make_tcp_server_descriptor(const connection_info &info)
{
    // Here you would normally create a real TCP server descriptor.
    // For this example, we return a mock descriptor.
    return std::make_shared<mock_tcp_server_descriptor>(info);
}
#endif