#include <webcraft/async/io/fs.hpp>
#include <webcraft/async/io/socket.hpp>
#include <webcraft/async/runtime.hpp>
#include <webcraft/async/runtime/windows.event.hpp>
#include <webcraft/async/runtime/macos.event.hpp>
#include <webcraft/async/runtime/linux.event.hpp>

using namespace webcraft::async;

#ifdef __linux__
#include <unistd.h>
#include <arpa/inet.h>  // inet_ntop, ntohs, htons
#include <netdb.h>      // getaddrinfo, getnameinfo
#include <netinet/in.h> // sockaddr_in, sockaddr_in6
#include <sys/socket.h> // socket functions, sockaddr_storage

int ios_to_posix(std::ios_base::openmode mode)
{
    int flags = 0;

    if ((mode & std::ios::in) && (mode & std::ios::out))
    {
        flags |= O_RDWR;
    }
    else if (mode & std::ios::in)
    {
        flags |= O_RDONLY;
    }
    else if (mode & std::ios::out)
    {
        flags |= O_WRONLY;
    }

    if (mode & std::ios::trunc)
    {
        flags |= O_TRUNC;
    }

    if (mode & std::ios::app)
    {
        flags |= O_APPEND;
    }

    if (mode & std::ios::out)
    {
        flags |= O_CREAT; // often needed with out/app/trunc
    }

    return flags;
}

std::pair<std::string, uint16_t> addr_to_host_port(
    const struct sockaddr_storage &addr)
{
    char host[NI_MAXHOST];
    char service[NI_MAXSERV];

    if (getnameinfo((struct sockaddr *)&addr, sizeof(addr),
                    host, sizeof(host),
                    service, sizeof(service),
                    NI_NUMERICHOST | NI_NUMERICSERV) != 0)
    {
        return {"", 0}; // Failed
    }

    return {std::string(host), static_cast<uint16_t>(std::stoi(service))};
}

class io_uring_file_descriptor : public webcraft::async::io::fs::detail::file_descriptor
{
private:
    int fd;
    bool closed{false};

public:
    io_uring_file_descriptor(int fd, std::ios_base::openmode mode) : file_descriptor(mode), fd(fd)
    {
    }

    ~io_uring_file_descriptor() = default;

    // virtual because we want to allow platform specific implementation
    task<size_t> read(std::span<char> buffer) override
    {
        if ((mode & std::ios::in) != std::ios::in)
        {
            throw std::ios_base::failure("File not open for reading");
        }

        int fd = this->fd;
        auto event = webcraft::async::detail::as_awaitable(webcraft::async::detail::linux::create_io_uring_event([fd, buffer](struct io_uring_sqe *sqe)
                                                                                                                 { io_uring_prep_read(sqe, fd, buffer.data(), buffer.size(), 0); }));

        co_await event;

        co_return event->get_result();
    }

    task<size_t> write(std::span<char> buffer) override
    {
        if ((mode & std::ios::out) != std::ios::out)
        {
            throw std::ios_base::failure("File not open for writing");
        }

        int fd = this->fd;
        auto event = webcraft::async::detail::as_awaitable(webcraft::async::detail::linux::create_io_uring_event([fd, buffer](struct io_uring_sqe *sqe)
                                                                                                                 { io_uring_prep_write(sqe, fd, buffer.data(), buffer.size(), 0); }));

        co_await event;

        co_return event->get_result();
    }

    task<void> close() override
    {
        if (closed)
            return;

        int fd = this->fd;

        co_await webcraft::async::detail::as_awaitable(webcraft::async::detail::linux::create_io_uring_event([fd](struct io_uring_sqe *sqe)
                                                                                                             { io_uring_prep_close(sqe, fd); }));

        closed = true;
    }
};

task<std::shared_ptr<file_descriptor>> webcraft::async::io::fs::detail::make_file_descriptor(std::filesystem::path p, std::ios_base::openmode mode)
{
    int flags = ios_to_posix(mode);

    auto event = webcraft::async::detail::linux::create_io_uring_event([flags, p](struct io_uring_sqe *sqe)
                                                                       { io_uring_prep_open(sqe, p.c_str(), flags, 0644); });

    co_await webcraft::async::detail::as_awaitable(event);

    int fd = event->get_result();
    if (fd < 0)
    {
        throw std::ios_base::failure("Failed to open file");
    }

    co_return std::make_shared<io_uring_file_descriptor>(fd, mode);
}

class io_uring_tcp_socket_descriptor : public tcp_socket_descriptor
{
private:
    int fd;
    bool closed{false};

    std::string host;
    uint16_t port;

public:
    io_uring_tcp_socket_descriptor() = default;

    io_uring_tcp_socket_descriptor(int fd, std::string host, uint16_t port) : fd(fd), host(std::move(host)), port(port)
    {
    }

    ~io_uring_tcp_socket_descriptor() = default;

    task<void> close() override
    {
        if (closed)
            return;

        int fd = this->fd;

        co_await webcraft::async::detail::as_awaitable(webcraft::async::detail::linux::create_io_uring_event([fd](struct io_uring_sqe *sqe)
                                                                                                             { io_uring_prep_close(sqe, fd); }));

        closed = true;
    }

    // virtual because we want to allow platform specific implementation
    task<size_t> read(std::span<char> buffer) override
    {
        int fd = this->fd;
        auto event = webcraft::async::detail::as_awaitable(webcraft::async::detail::linux::create_io_uring_event([fd, buffer](struct io_uring_sqe *sqe)
                                                                                                                 { io_uring_prep_read(sqe, fd, buffer.data(), buffer.size(), 0); }));

        co_await event;

        if (event->get_result() < 0)
        {
            throw std::ios_base::failure("Failed to connect with error: " + std::to_string(event->get_result()));
        }

        co_return event->get_result();
    }

    task<size_t> write(std::span<char> buffer) override
    {
        int fd = this->fd;
        auto event = webcraft::async::detail::as_awaitable(webcraft::async::detail::linux::create_io_uring_event([fd, buffer](struct io_uring_sqe *sqe)
                                                                                                                 { io_uring_prep_write(sqe, fd, buffer.data(), buffer.size(), 0); }));

        co_await event;

        if (event->get_result() < 0)
        {
            throw std::ios_base::failure("Failed to connect with error: " + std::to_string(event->get_result()));
        }

        co_return event->get_result();
    }

    task<void> connect(const webcraft::async::io::socket::connection_info &info) override
    {
        this->host = info.host;
        this->port = info.port;

        // Prepare address string for getaddrinfo
        std::string port_str = std::to_string(info.port);
        struct addrinfo hints{};
        hints.ai_family = AF_UNSPEC;     // Allow IPv4 or IPv6
        hints.ai_socktype = SOCK_STREAM; // TCP
        hints.ai_flags = AI_PASSIVE;     // No special flags
        hints.ai_protocol = IPPROTO_TCP;

        struct addrinfo *res;
        int ret = getaddrinfo(info.host.c_str(), port_str.c_str(), &hints, &res);
        if (ret != 0)
        {
            throw std::runtime_error(std::string("getaddrinfo failed: ") + gai_strerror(ret));
        }

        // We'll just take the first result
        const struct sockaddr *addr = res->ai_addr, *rp;
        socklen_t addr_len = res->ai_addrlen;

        for (rp = res; rp != nullptr; rp = rp->ai_next)
        {
            {
                auto event = webcraft::async::detail::as_awaitable(
                    webcraft::async::detail::linux::create_io_uring_event(
                        [rp](struct io_uring_sqe *sqe)
                        {
                            io_uring_prep_socket(sqe, rp->ai_family, rp->ai_socktype, rp->ai_protocol, SOCK_CLOEXEC | SOCK_NONBLOCK);
                        }));

                co_await event;

                if (event->get_result() < 0)
                {
                    continue;
                }

                this->fd = event->get_result();
            }

            {

                int fd = this->fd;

                // Await io_uring connect
                auto event = webcraft::async::detail::as_awaitable(
                    webcraft::async::detail::linux::create_io_uring_event(
                        [fd, addr, addr_len](struct io_uring_sqe *sqe)
                        {
                            io_uring_prep_connect(sqe, fd, addr, addr_len);
                        }));

                co_await event;

                if (event->get_result())
                {
                    break;
                }

                co_await webcraft::async::detail::as_awaitable(webcraft::async::detail::linux::create_io_uring_event([fd](struct io_uring_sqe *sqe)
                                                                                                                     { io_uring_prep_close(sqe, fd); }));
            }
        }

        freeaddrinfo(res); // Free memory allocated by getaddrinfo

        if (rp == nullptr)
        {
            throw std::ios_base::failure("Failed to connect: No valid address found");
        }

        co_return;
    }

    task<void> shutdown(webcraft::async::io::socket::socket_stream_mode mode)
    {
        int fd = this->fd;

        if (mode == webcraft::async::io::socket::socket_stream_mode::READ)
        {
            co_await webcraft::async::detail::as_awaitable(webcraft::async::detail::linux::create_io_uring_event([fd](struct io_uring_sqe *sqe)
                                                                                                                 { io_uring_prep_shutdown(sqe, fd, SHUT_RD); }));
        }
        else
        {
            co_await webcraft::async::detail::as_awaitable(webcraft::async::detail::linux::create_io_uring_event([fd](struct io_uring_sqe *sqe)
                                                                                                                 { io_uring_prep_shutdown(sqe, fd, SHUT_WR); }));
        }

        co_return;
    }

    std::string get_remote_host() override
    {
        return host;
    }

    uint16_t get_remote_port() override
    {
        return port;
    }
};

class io_uring_tcp_listener_descriptor : public webcraft::async::io::socket::tcp_listener_descriptor
{
private:
    int fd;
    bool closed{false};

public:
    io_uring_tcp_listener_descriptor()
    {
    }

    ~io_uring_tcp_listener_descriptor() = default;

    task<void> close() override
    {
        if (closed)
            return;

        int fd = this->fd;

        co_await webcraft::async::detail::as_awaitable(webcraft::async::detail::linux::create_io_uring_event([fd](struct io_uring_sqe *sqe)
                                                                                                             { io_uring_prep_close(sqe, fd); }));

        closed = true;
    }

    task<void> bind(const webcraft::async::io::socket::connection_info &info) override
    {
        // Prepare address string for getaddrinfo
        std::string port_str = std::to_string(info.port);
        struct addrinfo hints{};
        hints.ai_family = AF_UNSPEC;     // Allow IPv4 or IPv6
        hints.ai_socktype = SOCK_STREAM; // TCP
        hints.ai_flags = AI_PASSIVE;     // No special flags
        hints.ai_protocol = IPPROTO_TCP;

        struct addrinfo *res;
        int ret = getaddrinfo(info.host.c_str(), port_str.c_str(), &hints, &res);
        if (ret != 0)
        {
            throw std::runtime_error(std::string("getaddrinfo failed: ") + gai_strerror(ret));
        }

        // We'll just take the first result
        const struct sockaddr *addr = res->ai_addr, *rp;
        socklen_t addr_len = res->ai_addrlen;

        for (rp = res; rp != nullptr; rp = rp->ai_next)
        {
            {
                auto event = webcraft::async::detail::as_awaitable(
                    webcraft::async::detail::linux::create_io_uring_event(
                        [rp](struct io_uring_sqe *sqe)
                        {
                            io_uring_prep_socket(sqe, rp->ai_family, rp->ai_socktype, rp->ai_protocol, SOCK_CLOEXEC | SOCK_NONBLOCK);
                        }));

                co_await event;

                if (event->get_result() < 0)
                {
                    continue;
                }

                this->fd = event->get_result();
            }

            {

                int fd = this->fd;

                // Await io_uring connect
                auto event = webcraft::async::detail::as_awaitable(
                    webcraft::async::detail::linux::create_io_uring_event(
                        [fd, rp](struct io_uring_sqe *sqe)
                        {
                            io_uring_prep_bind(sqe, fd, rp->ai_addr, rp->ai_addrlen);
                        }));

                co_await event;

                if (event->get_result())
                {
                    break;
                }

                co_await webcraft::async::detail::as_awaitable(webcraft::async::detail::linux::create_io_uring_event([fd](struct io_uring_sqe *sqe)
                                                                                                                     { io_uring_prep_close(sqe, fd); }));
            }
        }

        freeaddrinfo(res); // Free memory allocated by getaddrinfo

        if (rp == nullptr)
        {
            throw std::ios_base::failure("Failed to connect: No valid address found");
        }
    }

    task<void> listen(int backlog) override
    {
        int fd = this->fd;

        co_await webcraft::async::detail::as_awaitable(webcraft::async::detail::linux::create_io_uring_event([fd, backlog](struct io_uring_sqe *sqe)
                                                                                                             { io_uring_prep_listen(sqe, fd, backlog); }));

        co_return;
    }

    task<std::unique_ptr<webcraft::async::io::socket::tcp_listener_descriptor>> accept() override
    {
        int fd = this->fd;
        struct sockaddr_storage addr;
        socklen_t addr_len = sizeof(addr);

        auto event = webcraft::async::detail::as_awaitable(webcraft::async::detail::linux::create_io_uring_event([fd, &addr, addr_len](struct io_uring_sqe *sqe)
                                                                                                                 { io_uring_prep_accept(sqe, fd, (struct sockaddr *)&addr, &addr_len, SOCK_CLOEXEC | SOCK_NONBLOCK); }));

        co_await event;

        if (event->get_result() < 0)
        {
            throw std::ios_base::failure("Failed to accept connection: " + std::to_string(event->get_result()));
        }

        auto [host, port] = addr_to_host_port(addr);

        if (host.empty() || port == 0)
        {
            co_return nullptr;
        }

        co_return std::make_unique<io_uring_tcp_socket_descriptor>(event->get_result(), host, port);
    }
};

task<std::shared_ptr<webcraft::async::io::socket::detail::tcp_socket_descriptor>> webcraft::async::io::socket::detail::make_tcp_socket_descriptor()
{
    co_return std::make_shared<io_uring_tcp_socket_descriptor>();
}

task<std::shared_ptr<webcraft::async::io::socket::detail::tcp_listener_descriptor>> webcraft::async::io::socket::detail::make_tcp_listener_descriptor()
{
    co_return std::make_shared<io_uring_tcp_listener_descriptor>();
}

#elif defined(_WIN32)
#elif defined(__APPLE__)
#else

#endif