///////////////////////////////////////////////////////////////////////////////
// Copyright (c) Aditya Rao
// Licenced under MIT license. See LICENSE.txt for details.
///////////////////////////////////////////////////////////////////////////////

#include <webcraft/async/io/io.hpp>
#include <webcraft/async/runtime.hpp>
#include <webcraft/async/task_completion_source.hpp>
#include <webcraft/async/runtime/windows.event.hpp>
#include <webcraft/async/runtime/macos.event.hpp>
#include <webcraft/async/runtime/linux.event.hpp>
#include <cstdio>
#include <webcraft/async/thread_pool.hpp>
#include <webcraft/async/async_event.hpp>
#include <webcraft/net/util.hpp>

using namespace webcraft::async;
using namespace webcraft::async::io::socket::detail;

#if defined(__linux__)

#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>

#elif defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <MSWSock.h>

#elif defined(__APPLE__)

#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>

#endif

using async_on_address_resolved = std::function<webcraft::async::task<bool>(sockaddr *addr, socklen_t addrlen)>;

webcraft::async::task<bool> async_host_port_to_addr(const webcraft::async::io::socket::connection_info &info, async_on_address_resolved callback)
{
    struct addrinfo hints{};
    hints.ai_family = AF_UNSPEC;    // Allow IPv4 or IPv6
    hints.ai_socktype = SOCK_DGRAM; // UDP

    struct addrinfo *res;
    int ret = getaddrinfo(info.host.c_str(), std::to_string(info.port).c_str(), &hints, &res);
    if (ret != 0)
    {
        co_return false;
    }

    // Call the callback with the resolved address
    bool check = false;

    for (auto *rp = res; rp; rp = rp->ai_next)
    {
        check = co_await callback(rp->ai_addr, (socklen_t)rp->ai_addrlen);
        if (check)
            break;
        else
            continue;
    }

    freeaddrinfo(res);
    co_return check;
}

#ifdef WEBCRAFT_UDP_MOCK

#ifndef _WIN32
#define SOCKET int

int get_last_socket_error()
{
    return errno;
}

char *get_error_string(int error)
{
    return strerror(error);
}
#else

int get_last_socket_error()
{
    return WSAGetLastError();
}

char *get_error_string(int err)
{
    static char buf[256];
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL, err,
                   MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                   buf, (sizeof(buf) / sizeof(char)), NULL);
    return buf;
}

#endif

class mock_udp_socket_descriptor : public webcraft::async::io::socket::detail::udp_socket_descriptor
{
private:
    SOCKET socket;
    std::atomic<bool> closed{false};

    void create_socket_if_not_exists(std::optional<webcraft::async::io::socket::ip_version> &version)
    {
        if (socket == -1)
        {
            // try ipv6 udp first for default
            if (version == std::nullopt)
            {
                socket = ::socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);

                // if ipv6 isn't supported fallback to ipv4
                if (socket < 0)
                {
#ifdef _WIN32
                    if (WSAGetLastError() != WSAEAFNOSUPPORT)
#else
                    if (errno != EAFNOSUPPORT)
#endif
                    {
                        socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
                    }
                }
            }
            // otherwise they want to force ip implementation then use it
            else if (version == webcraft::async::io::socket::ip_version::IPv6)
            {
                socket = ::socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
            }
            else
            {
                socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            }

            // if error creating socket throw
            if (socket < 0)
            {
#ifdef _WIN32
                auto err = WSAGetLastError();
#else
                auto err = errno;
#endif
                throw std::ios_base::failure("Failed to create UDP socket: " + std::to_string(err));
            }
        }
    }

    void close_socket()
    {
#ifdef _WIN32
        ::closesocket(socket);
#else
        ::close(socket);
#endif
        socket = -1;
    }

public:
    mock_udp_socket_descriptor(std::optional<webcraft::async::io::socket::ip_version> version) : webcraft::async::io::socket::detail::udp_socket_descriptor(version), socket(-1)
    {
        create_socket_if_not_exists(version);
    }

    ~mock_udp_socket_descriptor()
    {
        fire_and_forget(close());
    }

    task<void> close() override
    {
        if (socket != -1)
        {
            bool expected = false;
            if (closed.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            {
                close_socket();
            }
        }
        co_return;
    }

    void bind(const webcraft::async::io::socket::connection_info &info) override
    {
        close_socket();

        // Prepare address string for getaddrinfo
        std::string port_str = std::to_string(info.port);
        struct addrinfo hints{};
        hints.ai_family = AF_UNSPEC;    // Allow IPv4 or IPv6
        hints.ai_socktype = SOCK_DGRAM; // UDP
        hints.ai_protocol = IPPROTO_UDP;

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
            socklen_t len = (socklen_t)rp->ai_addrlen;

            SOCKET fd = ::socket(family, sock_type, protocol);
            if (fd < 0)
            {
                continue;
            }

#ifndef _WIN32
            int opt = 1;
            if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
            {
                close_socket();
                continue;
            }
#endif

            // Await io_uring bind
            int result = ::bind(fd, addr, len);

            if (result < 0)
            {
                close_socket();
                continue;
            }
            else
            {
                this->socket = fd;
                flag = true;
                break;
            }
        }

        freeaddrinfo(res); // Free memory allocated by getaddrinfo

        if (!flag)
            throw std::ios_base::failure("Failed to create socket: " + std::string(get_error_string(get_last_socket_error())));
    }

    task<size_t> recvfrom(std::span<char> buffer, webcraft::async::io::socket::connection_info &info) override
    {
        if (closed)
            co_return 0;

        sockaddr_storage addr;
        socklen_t addr_len = sizeof(addr);

        auto size = ::recvfrom(socket, buffer.data(), (int)buffer.size(), 0, (sockaddr *)&addr, &addr_len);
        if (size < 0)
        {
            throw std::ios_base::failure("Failed to receive data: " + std::string(get_error_string(get_last_socket_error())));
        }

        // Extract connection info
        auto [host, port] = webcraft::net::util::addr_to_host_port(addr);

        info.host = host;
        info.port = port;

        co_return size;
    }

    task<size_t> sendto(std::span<const char> buffer, const webcraft::async::io::socket::connection_info &info) override
    {
        if (closed)
            co_return 0;

        SOCKET socket = this->socket;
        int bytes_sent = -1;
        bool flag = webcraft::net::util::host_port_to_addr(
            info,
            [&bytes_sent, buffer, socket](sockaddr *addr, socklen_t len)
            {
                bytes_sent = ::sendto(socket, buffer.data(), (int)buffer.size(), 0, addr, len);
                return bytes_sent >= 0;
            });

        if (!flag)
            throw std::ios_base::failure("Failed to send data: " + std::string(get_error_string(get_last_socket_error())));

        co_return bytes_sent;
    }
};

std::shared_ptr<webcraft::async::io::socket::detail::udp_socket_descriptor> webcraft::async::io::socket::detail::make_udp_socket_descriptor(std::optional<webcraft::async::io::socket::ip_version> version)
{
    return std::make_shared<mock_udp_socket_descriptor>(version);
}
#elif defined(_WIN32)

class iocp_udp_socket_descriptor : public webcraft::async::io::socket::detail::udp_socket_descriptor
{
private:
    SOCKET socket;
    std::atomic<bool> closed{false};

    void create_socket_if_not_exists(std::optional<webcraft::async::io::socket::ip_version> &version)
    {
        if (socket == -1)
        {
            // try ipv6 udp first for default
            if (version == std::nullopt)
            {
                socket = ::socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);

                // if ipv6 isn't supported fallback to ipv4
                if (socket == INVALID_SOCKET)
                {
                    if (WSAGetLastError() != WSAEAFNOSUPPORT)
                    {
                        socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
                    }
                }
            }
            // otherwise they want to force ip implementation then use it
            else if (version == webcraft::async::io::socket::ip_version::IPv6)
            {
                socket = ::socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
            }
            else
            {
                socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            }

            // if error creating socket throw
            if (socket == INVALID_SOCKET)
            {
                auto err = WSAGetLastError();

                throw std::ios_base::failure("Failed to create UDP socket: " + std::to_string(err));
            }
        }
    }

    void close_socket()
    {
        ::closesocket(socket);
        socket = INVALID_SOCKET;
    }

public:
    iocp_udp_socket_descriptor(std::optional<webcraft::async::io::socket::ip_version> version) : webcraft::async::io::socket::detail::udp_socket_descriptor(version), socket(INVALID_SOCKET)
    {
        create_socket_if_not_exists(version);
    }

    ~iocp_udp_socket_descriptor()
    {
        fire_and_forget(close());
    }

    task<void> close() override
    {
        if (socket != INVALID_SOCKET)
        {
            bool expected = false;
            if (closed.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            {
                close_socket();
            }
        }
        co_return;
    }

    void bind(const webcraft::async::io::socket::connection_info &info) override
    {
        close_socket();

        // Prepare address string for getaddrinfo
        std::string port_str = std::to_string(info.port);
        struct addrinfo hints{};
        hints.ai_family = AF_UNSPEC;    // Allow IPv4 or IPv6
        hints.ai_socktype = SOCK_DGRAM; // UDP
        hints.ai_protocol = IPPROTO_UDP;

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
            socklen_t len = (socklen_t)rp->ai_addrlen;

            SOCKET fd = ::socket(family, sock_type, protocol);
            if (fd == INVALID_SOCKET)
            {
                continue;
            }

            int opt = 1;
            if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
            {
                close_socket();
                continue;
            }

            // Await io_uring bind
            int result = ::bind(fd, addr, len);

            if (result < 0)
            {
                close_socket();
                continue;
            }
            else
            {
                this->socket = fd;
                flag = true;
                break;
            }
        }

        freeaddrinfo(res); // Free memory allocated by getaddrinfo

        if (!flag)
            throw std::ios_base::failure("Failed to create socket: " + std::to_string(WSAGetLastError()));
    }

    task<size_t> recvfrom(std::span<char> buffer, webcraft::async::io::socket::connection_info &info) override
    {
        if (closed)
            co_return 0;

        sockaddr_storage addr;
        socklen_t addr_len = sizeof(addr);

        SOCKET fd = this->socket;
        WSABUF wsabuf;
        wsabuf.buf = const_cast<char *>(buffer.data());
        wsabuf.len = (ULONG)buffer.size();
        DWORD flags = 0;

        auto event = webcraft::async::detail::as_awaitable(
            webcraft::async::detail::windows::create_async_socket_overlapped_event(
                fd,
                [fd, &wsabuf, &flags, &addr, &addr_len](LPDWORD numberOfBytesTransfered, LPOVERLAPPED overlapped)
                {
                    return WSARecvFrom(fd,
                                       &wsabuf,
                                       1,
                                       numberOfBytesTransfered,
                                       &flags,
                                       (sockaddr *)&addr,
                                       &addr_len,
                                       overlapped,
                                       nullptr);
                }));

        co_await event;

        auto size = event.get_result();

        if (size < 0)
        {
            throw std::ios_base::failure("Failed to receive data: " + std::to_string(WSAGetLastError()));
        }

        // Extract connection info
        auto [host, port] = webcraft::net::util::addr_to_host_port(addr);

        info.host = host;
        info.port = port;

        co_return size;
    }

    task<size_t> sendto(std::span<const char> buffer, const webcraft::async::io::socket::connection_info &info) override
    {
        if (closed)
            co_return 0;

        SOCKET socket = this->socket;
        int bytes_sent = -1;

        std::function<task<bool>(sockaddr *, socklen_t)> async_func = [&bytes_sent, buffer, socket](sockaddr *addr, socklen_t len) -> task<bool>
        {
            WSABUF wsabuf;
            wsabuf.buf = const_cast<char *>(buffer.data());
            wsabuf.len = (ULONG)buffer.size();

            auto event = webcraft::async::detail::as_awaitable(
                webcraft::async::detail::windows::create_async_socket_overlapped_event(
                    socket,
                    [socket, &wsabuf, addr, len](LPDWORD numberOfBytesTransfered, LPWSAOVERLAPPED overlapped)
                    {
                        return WSASendTo(
                            socket,
                            &wsabuf,
                            1,
                            numberOfBytesTransfered,
                            0,
                            (sockaddr *)addr,
                            len,
                            overlapped,
                            nullptr);
                    }));

            co_await event;

            bytes_sent = event.get_result();

            co_return bytes_sent >= 0;
        };

        bool flag = co_await async_host_port_to_addr(
            info, async_func);

        if (!flag)
            throw std::ios_base::failure("Failed to send data: " + std::to_string(WSAGetLastError()));

        co_return bytes_sent;
    }
};

std::shared_ptr<webcraft::async::io::socket::detail::udp_socket_descriptor> webcraft::async::io::socket::detail::make_udp_socket_descriptor(std::optional<webcraft::async::io::socket::ip_version> version)
{
    return std::make_shared<iocp_udp_socket_descriptor>(version);
}

#elif defined(__linux__)

class io_uring_udp_socket_descriptor : public webcraft::async::io::socket::detail::udp_socket_descriptor
{
private:
    int socket;
    std::atomic<bool> closed{false};

    void create_socket_if_not_exists(std::optional<webcraft::async::io::socket::ip_version> &version)
    {
        if (socket == -1)
        {
            // try ipv6 udp first for default
            if (version == std::nullopt)
            {
                socket = ::socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);

                // if ipv6 isn't supported fallback to ipv4
                if (socket < 0)
                {
                    if (errno != EAFNOSUPPORT)
                    {
                        socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
                    }
                }
            }
            // otherwise they want to force ip implementation then use it
            else if (version == webcraft::async::io::socket::ip_version::IPv6)
            {
                socket = ::socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
            }
            else
            {
                socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            }

            // if error creating socket throw
            if (socket < 0)
            {
                auto err = errno;

                throw std::ios_base::failure("Failed to create UDP socket: " + std::to_string(err));
            }
        }
    }

    void close_socket()
    {
        ::close(socket);
        socket = -1;
    }

public:
    io_uring_udp_socket_descriptor(std::optional<webcraft::async::io::socket::ip_version> version) : webcraft::async::io::socket::detail::udp_socket_descriptor(version), socket(-1)
    {
        create_socket_if_not_exists(version);
    }

    ~io_uring_udp_socket_descriptor()
    {
        fire_and_forget(close());
    }

    task<void> close() override
    {
        if (socket != -1)
        {
            bool expected = false;
            if (closed.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            {
                close_socket();
            }
        }
        co_return;
    }

    void bind(const webcraft::async::io::socket::connection_info &info) override
    {
        close_socket();

        // Prepare address string for getaddrinfo
        std::string port_str = std::to_string(info.port);
        struct addrinfo hints{};
        hints.ai_family = AF_UNSPEC;    // Allow IPv4 or IPv6
        hints.ai_socktype = SOCK_DGRAM; // UDP
        hints.ai_protocol = IPPROTO_UDP;

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
            socklen_t len = (socklen_t)rp->ai_addrlen;

            int fd = ::socket(family, sock_type, protocol);
            if (fd < 0)
            {
                continue;
            }

            int opt = 1;
            if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
            {
                close_socket();
                continue;
            }

            // Await io_uring bind
            int result = ::bind(fd, addr, len);

            if (result < 0)
            {
                close_socket();
                continue;
            }
            else
            {
                this->socket = fd;
                flag = true;
                break;
            }
        }

        freeaddrinfo(res); // Free memory allocated by getaddrinfo

        if (!flag)
            throw std::ios_base::failure("Failed to create socket: " + std::string(strerror(errno)));
    }

    task<size_t> recvfrom(std::span<char> buffer, webcraft::async::io::socket::connection_info &info) override
    {
        if (closed)
            co_return 0;

        sockaddr_storage addr;
        socklen_t addr_len = sizeof(addr);

        // setup iovec for the buffer
        iovec iov;
        iov.iov_base = buffer.data();
        iov.iov_len = buffer.size();

        // set up msghdr
        msghdr msg{};
        msg.msg_name = &addr;
        msg.msg_namelen = addr_len;
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        msg.msg_control = nullptr;
        msg.msg_controllen = 0;
        msg.msg_flags = 0;

        int fd = socket;
        auto event = webcraft::async::detail::as_awaitable(
            webcraft::async::detail::linux::create_io_uring_event(
                [fd, &msg](struct io_uring_sqe *sqe)
                {
                    io_uring_prep_recvmsg(sqe, fd, &msg, 0);
                }));

        co_await event;

        if (event.get_result() < 0)
        {
            throw std::ios_base::failure("Failed to receive data: " + std::string(strerror(errno)));
        }

        // Extract connection info
        auto [host, port] = webcraft::net::util::addr_to_host_port(addr);

        info.host = host;
        info.port = port;

        co_return event.get_result();
    }

    task<size_t> sendto(std::span<const char> buffer, const webcraft::async::io::socket::connection_info &info) override
    {
        if (closed)
            co_return 0;

        int socket = this->socket;
        int bytes_sent = -1;

        std::function<task<bool>(sockaddr *, socklen_t)> async_func = [&bytes_sent, buffer, socket](sockaddr *addr, socklen_t len) -> task<bool>
        {
            auto event = webcraft::async::detail::as_awaitable(
                webcraft::async::detail::linux::create_io_uring_event(
                    [socket, buffer, addr, len](struct io_uring_sqe *sqe)
                    {
                        io_uring_prep_sendto(sqe, socket, buffer.data(), buffer.size(), 0, addr, len);
                    }));

            co_await event;

            bytes_sent = event.get_result();

            co_return bytes_sent >= 0;
        };

        bool flag = co_await async_host_port_to_addr(
            info, async_func);

        if (!flag)
            throw std::ios_base::failure("Failed to send data: " + std::string(strerror(errno)));

        co_return bytes_sent;
    }
};

std::shared_ptr<webcraft::async::io::socket::detail::udp_socket_descriptor> webcraft::async::io::socket::detail::make_udp_socket_descriptor(std::optional<webcraft::async::io::socket::ip_version> version)
{
    return std::make_shared<io_uring_udp_socket_descriptor>(version);
}

#elif defined(__APPLE__)
#endif