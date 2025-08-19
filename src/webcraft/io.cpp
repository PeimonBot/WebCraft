#include <webcraft/async/io/io.hpp>
#include <webcraft/async/runtime.hpp>
#include <webcraft/async/runtime/windows.event.hpp>
#include <webcraft/async/runtime/macos.event.hpp>
#include <webcraft/async/runtime/linux.event.hpp>
#include <cstdio>

using namespace webcraft::async;
using namespace webcraft::async::io::fs;
using namespace webcraft::async::io::fs::detail;
using namespace webcraft::async::io::socket::detail;

#if defined(WEBCRAFT_MOCK_FS_TESTS)

class sync_file_descriptor : public file_descriptor
{
private:
    std::FILE *file;

public:
    sync_file_descriptor(std::filesystem::path p, std::ios_base::openmode mode) : file_descriptor(mode)
    {
        if (mode & std::ios::in)
        {
            file = std::fopen(p.string().c_str(), "r");
        }
        else if (mode & std::ios::out)
        {
            if (mode & std::ios::app)
            {
                file = std::fopen(p.string().c_str(), "a");
            }
            else
            {
                file = std::fopen(p.string().c_str(), "w");
            }
        }
    }

    ~sync_file_descriptor()
    {
        if (file)
        {
            fire_and_forget(close());
            file = nullptr;
        }
    }

    // virtual because we want to allow platform specific implementation
    task<size_t> read(std::span<char> buffer)
    {
        co_return std::fread(buffer.data(), sizeof(char), buffer.size(), file);
    }

    task<size_t> write(std::span<char> buffer)
    {
        co_return std::fwrite(buffer.data(), sizeof(char), buffer.size(), file);
    }

    task<void> close()
    {
        if (file)
        {
            std::fclose(file);
            file = nullptr;
        }
        co_return;
    }
};

task<std::shared_ptr<file_descriptor>> webcraft::async::io::fs::detail::make_file_descriptor(std::filesystem::path p, std::ios_base::openmode mode)
{
    co_return std::make_shared<sync_file_descriptor>(p, mode);
}

#elif defined(__linux__)

// TODO: implement internal buffering for recv() for files so it doesn't just read one byte at a time

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

class io_uring_file_descriptor : public webcraft::async::io::fs::detail::file_descriptor
{
private:
    int fd;
    bool closed{false};

public:
    io_uring_file_descriptor(int fd, std::ios_base::openmode mode) : file_descriptor(mode), fd(fd)
    {
    }

    ~io_uring_file_descriptor()
    {
        if (!closed)
        {
            fire_and_forget(close());
        }
    }

    // virtual because we want to allow platform specific implementation
    task<size_t> read(std::span<char> buffer) override
    {
        if ((mode & std::ios::in) != std::ios::in)
        {
            throw std::ios_base::failure("File not open for reading");
        }

        int fd = this->fd;
        auto event = webcraft::async::detail::as_awaitable(webcraft::async::detail::linux::create_io_uring_event([fd, buffer](struct io_uring_sqe *sqe)
                                                                                                                 { io_uring_prep_read(sqe, fd, buffer.data(), buffer.size(), -1); }));

        co_await event;

        co_return event.get_result();
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

        co_return event.get_result();
    }

    task<void> close() override
    {
        if (closed)
            co_return;

        int fd = this->fd;

        co_await webcraft::async::detail::as_awaitable(webcraft::async::detail::linux::create_io_uring_event([fd](struct io_uring_sqe *sqe)
                                                                                                             { io_uring_prep_close(sqe, fd); }));

        closed = true;
    }
};

task<std::shared_ptr<file_descriptor>> webcraft::async::io::fs::detail::make_file_descriptor(std::filesystem::path p, std::ios_base::openmode mode)
{
    int flags = ios_to_posix(mode);

    auto event = webcraft::async::detail::as_awaitable(webcraft::async::detail::linux::create_io_uring_event([flags, p](struct io_uring_sqe *sqe)
                                                                                                             { io_uring_prep_open(sqe, p.c_str(), flags, 0644); }, {}));

    co_await event;

    int fd = event.get_result();
    if (fd < 0)
    {
        throw std::ios_base::failure("Failed to open file");
    }

    co_return std::make_shared<io_uring_file_descriptor>(fd, mode);
}

#elif defined(_WIN32)
#elif defined(__APPLE__)
#else
#endif

#if defined(WEBCRAFT_MOCK_SOCKET_TESTS)

task<std::shared_ptr<tcp_socket_descriptor>> webcraft::async::io::socket::detail::make_tcp_socket_descriptor()
{
    throw std::runtime_error("TCP socket descriptor not implemented in mock tests");
    co_return nullptr;
}

task<std::shared_ptr<tcp_listener_descriptor>> webcraft::async::io::socket::detail::make_tcp_listener_descriptor()
{
    throw std::runtime_error("TCP listener descriptor not implemented in mock tests");
    co_return nullptr;
}

#elif defined(__linux__)

#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <string.h>

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

class io_uring_tcp_socket_descriptor : public tcp_socket_descriptor
{
private:
    int fd;
    std::atomic<bool> closed{false};

    std::string host;
    uint16_t port;

public:
    io_uring_tcp_socket_descriptor()
    {
        fd = -1;
    }

    io_uring_tcp_socket_descriptor(int fd, std::string host, uint16_t port) : fd(fd), host(std::move(host)), port(port)
    {
    }

    ~io_uring_tcp_socket_descriptor()
    {
        fire_and_forget(close());
    }

    task<void> close() override
    {
        if (fd == -1)
            co_return;

        bool expected = false;
        if (closed.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        {
            int fd = this->fd;

            co_await webcraft::async::detail::as_awaitable(webcraft::async::detail::linux::create_io_uring_event([fd](struct io_uring_sqe *sqe)
                                                                                                                 { io_uring_prep_close(sqe, fd); }));
        }
    }

    task<size_t> read(std::span<char> buffer) override
    {
        int fd = this->fd;
        auto event = webcraft::async::detail::as_awaitable(webcraft::async::detail::linux::create_io_uring_event([fd, buffer](struct io_uring_sqe *sqe)
                                                                                                                 { io_uring_prep_recv(sqe, fd, buffer.data(), buffer.size(), 0); }));

        co_await event;

        if (event.get_result() < 0)
        {
            throw std::ios_base::failure("Failed to connect with error: " + std::to_string(event.get_result()));
        }

        co_return event.get_result();
    }

    task<size_t> write(std::span<const char> buffer) override
    {
        int fd = this->fd;
        auto event = webcraft::async::detail::as_awaitable(webcraft::async::detail::linux::create_io_uring_event([fd, buffer](struct io_uring_sqe *sqe)
                                                                                                                 { io_uring_prep_write(sqe, fd, buffer.data(), buffer.size(), 0); }));

        co_await event;

        if (event.get_result() < 0)
        {
            throw std::ios_base::failure("Failed to connect with error: " + std::to_string(event.get_result()));
        }

        co_return event.get_result();
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
        hints.ai_protocol = IPPROTO_TCP;

        struct addrinfo *res;
        int ret = getaddrinfo(info.host.c_str(), port_str.c_str(), &hints, &res);
        if (ret != 0)
        {
            throw std::runtime_error(std::string("getaddrinfo failed: ") + gai_strerror(ret));
        }

        bool flag = false;
        for (auto *rp = res; rp; rp = rp->ai_next)
        {

            int family = rp->ai_family;
            int sock_type = rp->ai_socktype;
            int protocol = rp->ai_protocol;
            const sockaddr *addr = rp->ai_addr;
            const socklen_t len = rp->ai_addrlen;

            int fd = socket(family, sock_type, protocol);
            if (fd < 0)
            {
                continue;
            }

            // Await io_uring connect
            auto event = webcraft::async::detail::as_awaitable(
                webcraft::async::detail::linux::create_io_uring_event(
                    [fd, addr, len](struct io_uring_sqe *sqe)
                    {
                        io_uring_prep_connect(sqe, fd, addr, len);
                    }));

            co_await event;

            if (event.get_result() < 0)
            {
                co_await webcraft::async::detail::as_awaitable(webcraft::async::detail::linux::create_io_uring_event([fd](struct io_uring_sqe *sqe)
                                                                                                                     { io_uring_prep_close(sqe, fd); }));
            }
            else
            {
                this->fd = fd;
                flag = true;
                break;
            }
        }

        freeaddrinfo(res); // Free memory allocated by getaddrinfo

        if (!flag)
            throw std::ios_base::failure("Failed to create socket: " + std::string(strerror(errno)));
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

task<std::shared_ptr<webcraft::async::io::socket::detail::tcp_socket_descriptor>> webcraft::async::io::socket::detail::make_tcp_socket_descriptor()
{
    co_return std::make_shared<io_uring_tcp_socket_descriptor>();
}

#endif

#ifdef WEBCRAFT_MOCK_LISTENER_TESTS

task<std::shared_ptr<webcraft::async::io::socket::detail::tcp_socket_descriptor>> webcraft::async::io::socket::detail::make_tcp_socket_descriptor()
{
    co_return std::make_shared<io_uring_tcp_socket_descriptor>();
}

task<std::shared_ptr<webcraft::async::io::socket::detail::tcp_listener_descriptor>> webcraft::async::io::socket::detail::make_tcp_listener_descriptor()
{
    throw std::runtime_error("Not implemented yet");
}

#elif defined(__linux__)

class io_uring_tcp_listener_descriptor : public webcraft::async::io::socket::detail::tcp_listener_descriptor
{
private:
    int fd;
    std::atomic<bool> closed{false};

public:
    io_uring_tcp_listener_descriptor()
    {
        fd = -1;
    }

    ~io_uring_tcp_listener_descriptor()
    {
        fire_and_forget(close());
    }

    task<void> close() override
    {
        if (fd == -1)
            co_return;

        bool expected = false;
        if (closed.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        {
            int fd = this->fd;

            co_await webcraft::async::detail::as_awaitable(webcraft::async::detail::linux::create_io_uring_event([fd](struct io_uring_sqe *sqe)
                                                                                                                 { io_uring_prep_close(sqe, fd); }));
        }
        co_return;
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

        bool flag = false;

        for (auto *rp = res; rp; rp = rp->ai_next)
        {

            int family = rp->ai_family;
            int sock_type = rp->ai_socktype;
            int protocol = rp->ai_protocol;
            sockaddr *addr = rp->ai_addr;
            socklen_t len = rp->ai_addrlen;

            int fd = socket(family, sock_type, protocol);
            if (fd < 0)
            {
                continue;
            }

            int opt = 1;
            if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
            {
                ::close(fd);
                continue;
            }

            // Await io_uring bind
            int result = ::bind(fd, addr, len);

            if (result < 0)
            {
                ::close(fd);
                continue;
            }
            else
            {
                this->fd = fd;
                flag = true;
                break;
            }
        }

        freeaddrinfo(res); // Free memory allocated by getaddrinfo

        if (!flag)
            throw std::ios_base::failure("Failed to create socket: " + std::string(strerror(errno)));
        co_return;
    }

    task<void> listen(int backlog) override
    {
        int fd = this->fd;

        int result = ::listen(fd, backlog);

        if (result < 0)
        {
            throw std::ios_base::failure("Failed to listen: " + std::to_string(result) + ", value: " + strerror(errno));
        }
        co_return;
    }

    task<std::shared_ptr<webcraft::async::io::socket::detail::tcp_socket_descriptor>> accept() override
    {
        int fd = this->fd;
        struct sockaddr_storage addr;
        socklen_t addr_len = sizeof(addr);

        auto event = webcraft::async::detail::as_awaitable(webcraft::async::detail::linux::create_io_uring_event([fd, &addr, addr_len](struct io_uring_sqe *sqe)
                                                                                                                 { io_uring_prep_accept(sqe, fd, (struct sockaddr *)&addr, (socklen_t *)&addr_len, SOCK_CLOEXEC); }));

        co_await event;

        if (event.get_result() < 0)
        {
            throw std::ios_base::failure("Failed to accept connection: " + std::to_string(event.get_result()) + ", value: " + strerror(-event.get_result()));
        }

        auto [host, port] = addr_to_host_port(addr);

        if (host.empty() || port == 0)
        {
            co_return nullptr;
        }

        co_return std::make_shared<io_uring_tcp_socket_descriptor>(event.get_result(), host, port);
    }
};

task<std::shared_ptr<webcraft::async::io::socket::detail::tcp_listener_descriptor>> webcraft::async::io::socket::detail::make_tcp_listener_descriptor()
{
    co_return std::make_shared<io_uring_tcp_listener_descriptor>();
}

#else

#endif
