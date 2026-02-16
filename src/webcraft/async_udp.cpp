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
#include <system_error>

using namespace webcraft::async;
using namespace webcraft::async::io::socket::detail;

#if defined(__linux__)

#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>

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
#include <netinet/in.h>

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
                std::error_code ec(err, std::system_category());
                throw std::system_error(ec, "Failed to create UDP socket");
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
        hints.ai_flags = AI_PASSIVE;

        struct addrinfo *res;
        int ret = getaddrinfo(info.host.c_str(), port_str.c_str(), &hints, &res);
        if (ret != 0)
        {
            throw webcraft::net::util::get_addr_info_error(ret);
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
        {
            std::error_code ec(get_last_socket_error(), std::system_category());
            throw std::system_error(ec, "Failed to create socket");
        }
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
            std::error_code ec(get_last_socket_error(), std::system_category());
            throw std::system_error(ec, "Failed to receive data");
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
        {
            std::error_code ec(get_last_socket_error(), std::system_category());
            throw std::system_error(ec, "Failed to send data");
        }
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

    // Helper to associate socket with the runtime's IOCP handle
    void associate_with_iocp(SOCKET socket)
    {
        if (socket == INVALID_SOCKET)
            return;

        HANDLE iocp = ::CreateIoCompletionPort(
            (HANDLE)socket,
            (HANDLE)webcraft::async::detail::get_native_handle(),
            0,
            0);

        if (iocp == NULL)
        {
            throw webcraft::async::detail::windows::overlapped_runtime_event_error("Failed to associate UDP socket with IOCP");
        }
    }

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
                throw webcraft::async::detail::windows::overlapped_winsock2_runtime_error("Failed to create UDP socket");
            }

            associate_with_iocp(socket);
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
        hints.ai_flags = AI_PASSIVE;

        struct addrinfo *res;
        int ret = getaddrinfo(info.host.c_str(), port_str.c_str(), &hints, &res);
        if (ret != 0)
        {
            throw webcraft::net::util::get_addr_info_error(ret);
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

            associate_with_iocp(fd);

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
        {
            throw webcraft::async::detail::windows::overlapped_winsock2_runtime_error("Failed to create UDP socket");
        }
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
            throw webcraft::async::detail::windows::overlapped_winsock2_runtime_error("Failed to receive data");
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
            throw webcraft::async::detail::windows::overlapped_winsock2_runtime_error("Failed to send data");

        co_return bytes_sent;
    }

    void join_group(const webcraft::async::io::socket::multicast_group &group, const webcraft::async::io::socket::multicast_join_options &) override
    {
        if (socket == INVALID_SOCKET) return;
        struct addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_flags = AI_NUMERICHOST;
        struct addrinfo *res = nullptr;
        if (getaddrinfo(group.host.c_str(), nullptr, &hints, &res) != 0 || !res)
            throw std::invalid_argument("Invalid multicast address: " + group.host);
        if (res->ai_family == AF_INET)
        {
            auto *sa = (struct sockaddr_in *)res->ai_addr;
            if (!IN_MULTICAST(ntohl(sa->sin_addr.s_addr)))
            {
                freeaddrinfo(res);
                throw std::invalid_argument("Not a multicast address: " + group.host);
            }
            ip_mreq mreq{};
            mreq.imr_multiaddr = sa->sin_addr;
            mreq.imr_interface.s_addr = INADDR_ANY;
            freeaddrinfo(res);
            if (setsockopt(socket, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *)&mreq, sizeof(mreq)) == SOCKET_ERROR)
                throw webcraft::async::detail::windows::overlapped_winsock2_runtime_error("Failed to join IPv4 multicast group");
            return;
        }
        if (res->ai_family == AF_INET6)
        {
            auto *sa = (struct sockaddr_in6 *)res->ai_addr;
            if (!IN6_IS_ADDR_MULTICAST(&sa->sin6_addr))
            {
                freeaddrinfo(res);
                throw std::invalid_argument("Not a multicast address: " + group.host);
            }
            ipv6_mreq mreq6{};
            mreq6.ipv6mr_multiaddr = sa->sin6_addr;
            mreq6.ipv6mr_interface = 0;
            freeaddrinfo(res);
            if (setsockopt(socket, IPPROTO_IPV6, IPV6_JOIN_GROUP, (char *)&mreq6, sizeof(mreq6)) == SOCKET_ERROR)
                throw webcraft::async::detail::windows::overlapped_winsock2_runtime_error("Failed to join IPv6 multicast group");
            return;
        }
        freeaddrinfo(res);
        throw std::invalid_argument("Invalid multicast address: " + group.host);
    }

    void leave_group(const webcraft::async::io::socket::multicast_group &group) override
    {
        if (socket == INVALID_SOCKET) return;
        struct addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_flags = AI_NUMERICHOST;
        struct addrinfo *res = nullptr;
        if (getaddrinfo(group.host.c_str(), nullptr, &hints, &res) != 0 || !res) return;
        if (res->ai_family == AF_INET)
        {
            auto *sa = (struct sockaddr_in *)res->ai_addr;
            ip_mreq mreq{};
            mreq.imr_multiaddr = sa->sin_addr;
            mreq.imr_interface.s_addr = INADDR_ANY;
            freeaddrinfo(res);
            setsockopt(socket, IPPROTO_IP, IP_DROP_MEMBERSHIP, (char *)&mreq, sizeof(mreq));
            return;
        }
        if (res->ai_family == AF_INET6)
        {
            auto *sa = (struct sockaddr_in6 *)res->ai_addr;
            ipv6_mreq mreq6{};
            mreq6.ipv6mr_multiaddr = sa->sin6_addr;
            mreq6.ipv6mr_interface = 0;
            freeaddrinfo(res);
            setsockopt(socket, IPPROTO_IPV6, IPV6_LEAVE_GROUP, (char *)&mreq6, sizeof(mreq6));
        }
        else
            freeaddrinfo(res);
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
                std::error_code ec(errno, std::system_category());
                throw std::system_error(ec, "Failed to create UDP socket");
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
        hints.ai_flags = AI_PASSIVE;

        struct addrinfo *res;
        int ret = getaddrinfo(info.host.c_str(), port_str.c_str(), &hints, &res);
        if (ret != 0)
        {
            throw webcraft::net::util::get_addr_info_error(ret);
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
        {
            std::error_code ec(errno, std::system_category());
            throw std::system_error(ec, "Failed to create socket");
        }
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
            std::error_code ec(errno, std::system_category());
            throw std::system_error(ec, "Failed to receive data");
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
            {
                std::error_code ec(errno, std::system_category());
                throw std::system_error(ec, "Failed to send data");
            }

            co_return bytes_sent;
        }

        void join_group(const webcraft::async::io::socket::multicast_group &group, const webcraft::async::io::socket::multicast_join_options &) override
        {
            if (socket < 0) return;
            in_addr maddr4;
            if (inet_pton(AF_INET, group.host.c_str(), &maddr4) == 1)
            {
                if (!IN_MULTICAST(ntohl(maddr4.s_addr)))
                    throw std::invalid_argument("Not a multicast address: " + group.host);
                struct ip_mreq mreq{};
                mreq.imr_multiaddr = maddr4;
                mreq.imr_interface.s_addr = INADDR_ANY;
                if (setsockopt(socket, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0)
                {
                    std::error_code ec(errno, std::system_category());
                    throw std::system_error(ec, "Failed to join IPv4 multicast group");
                }
                return;
            }
            struct in6_addr maddr6;
            if (inet_pton(AF_INET6, group.host.c_str(), &maddr6) == 1)
            {
                if (!IN6_IS_ADDR_MULTICAST(&maddr6))
                    throw std::invalid_argument("Not a multicast address: " + group.host);
                struct ipv6_mreq mreq6{};
                mreq6.ipv6mr_multiaddr = maddr6;
                mreq6.ipv6mr_interface = 0;
                if (setsockopt(socket, IPPROTO_IPV6, IPV6_JOIN_GROUP, &mreq6, sizeof(mreq6)) < 0)
                {
                    std::error_code ec(errno, std::system_category());
                    throw std::system_error(ec, "Failed to join IPv6 multicast group");
                }
                return;
            }
            throw std::invalid_argument("Invalid multicast address: " + group.host);
        }

        void leave_group(const webcraft::async::io::socket::multicast_group &group) override
        {
            if (socket < 0) return;
            in_addr maddr4;
            if (inet_pton(AF_INET, group.host.c_str(), &maddr4) == 1)
            {
                struct ip_mreq mreq{};
                mreq.imr_multiaddr = maddr4;
                mreq.imr_interface.s_addr = INADDR_ANY;
                setsockopt(socket, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq, sizeof(mreq));
                return;
            }
            struct in6_addr maddr6;
            if (inet_pton(AF_INET6, group.host.c_str(), &maddr6) == 1)
            {
                struct ipv6_mreq mreq6{};
                mreq6.ipv6mr_multiaddr = maddr6;
                mreq6.ipv6mr_interface = 0;
                setsockopt(socket, IPPROTO_IPV6, IPV6_LEAVE_GROUP, &mreq6, sizeof(mreq6));
            }
        }
    };

std::shared_ptr<webcraft::async::io::socket::detail::udp_socket_descriptor> webcraft::async::io::socket::detail::make_udp_socket_descriptor(std::optional<webcraft::async::io::socket::ip_version> version)
{
    return std::make_shared<io_uring_udp_socket_descriptor>(version);
}

#elif defined(__APPLE__)

#include "async_tcp_socket_decl.hpp"

class kqueue_udp_socket_descriptor : public webcraft::async::io::socket::detail::udp_socket_descriptor,
                                     public webcraft::async::detail::runtime_callback
{
private:
    int socket;
    std::atomic<bool> closed{false};
    int kq;

    async_single_resumer_latch read_event{};

    void create_socket_if_not_exists(std::optional<webcraft::async::io::socket::ip_version> &version)
    {
        if (socket == -1)
        {
            // try ipv6 udp first for default
            if (version == std::nullopt || version == webcraft::async::io::socket::ip_version::IPv6)
            {
                socket = ::socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
            }

            if (socket < 0 && (version == std::nullopt || version == webcraft::async::io::socket::ip_version::IPv4))
            {
                socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            }

            // if error creating socket throw
            if (socket < 0)
            {
                std::error_code ec(errno, std::system_category());
                throw std::system_error(ec, "Failed to create UDP socket");
            }

            // Enable multicast loopback on macOS so that send/receive on the same host
            // works (e.g. TestMulticastSendReceive). Without this, multicast packets
            // sent by the sender are not delivered to local receivers.
            int one = 1;
#if defined(SO_DOMAIN)
            int domain = 0;
            socklen_t domain_len = sizeof(domain);
            if (::getsockopt(socket, SOL_SOCKET, SO_DOMAIN, &domain, &domain_len) == 0)
            {
                if (domain == AF_INET)
                    ::setsockopt(socket, IPPROTO_IP, IP_MULTICAST_LOOP, &one, sizeof(one));
                else if (domain == AF_INET6)
                    ::setsockopt(socket, IPPROTO_IPV6, IPV6_MULTICAST_LOOP, &one, sizeof(one));
            }
#else
            // Fallback: set both IPv4 and IPv6 loop (only one will apply to the socket)
            ::setsockopt(socket, IPPROTO_IP, IP_MULTICAST_LOOP, &one, sizeof(one));
            ::setsockopt(socket, IPPROTO_IPV6, IPV6_MULTICAST_LOOP, &one, sizeof(one));
#endif

            register_with_queue();
        }
    }

    void close_socket()
    {
        deregister_with_queue();
        ::close(socket);
        socket = -1;
    }

public:
    kqueue_udp_socket_descriptor(std::optional<webcraft::async::io::socket::ip_version> version) : webcraft::async::io::socket::detail::udp_socket_descriptor(version), socket(-1)
    {
        create_socket_if_not_exists(version);
    }

    ~kqueue_udp_socket_descriptor()
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
        hints.ai_flags = AI_PASSIVE;

        struct addrinfo *res;
        int ret = getaddrinfo(info.host.c_str(), port_str.c_str(), &hints, &res);
        if (ret != 0)
        {
            throw webcraft::net::util::get_addr_info_error(ret);
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

            // Reuse address is often useful for UDP
            int on = 1;
            setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

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
        {
            std::error_code ec(errno, std::system_category());
            throw std::system_error(ec, "Failed to create socket");
        }

        register_with_queue();
    }

    // ---------------------------------------------------------
    // The Async Read Implementation (Lazy/On-Demand)
    // ---------------------------------------------------------
    task<size_t> recvfrom(std::span<char> buffer, webcraft::async::io::socket::connection_info &info) override
    {
        if (closed || socket == -1)
            co_return 0;

        while (true)
        {
            sockaddr_storage addr{};
            socklen_t addr_len = sizeof(addr);

            // 1. Try to read immediately (non-blocking)
            ssize_t size = ::recvfrom(socket, buffer.data(), (int)buffer.size(), 0, (sockaddr *)&addr, &addr_len);

            if (size >= 0)
            {
                // Success! Parse address and return.
                auto [host, port] = webcraft::net::util::addr_to_host_port(addr);
                info.host = host;
                info.port = port;
                co_return (size_t) size;
            }

            // 2. Handle Errors
            int err = errno;
            if (err == EAGAIN || err == EWOULDBLOCK)
            {
                // No data yet. Suspend coroutine and wait for kqueue to wake us.
                co_await read_event;

                // When we wake up, loop back to step 1 and try reading again.
                // We check 'closed' again in case socket was closed while we waited.
                if (closed)
                    co_return 0;
                continue;
            }
            else if (err == EINTR)
            {
                continue;
            }
            else
            {
                std::error_code ec(err, std::system_category());
                throw std::system_error(ec, "Failed to receive data");
            }
        }
    }

    task<size_t> sendto(std::span<const char> buffer, const webcraft::async::io::socket::connection_info &info) override
    {
        if (closed || socket == -1)
            co_return 0;
        int bytes_sent = -1;

        // Note: Because register_with_queue sets O_NONBLOCK, sendto *might* fail with EAGAIN
        // if the kernel buffer is full. Since you want sync writes, we technically should
        // spin or wait here, but for UDP, it is standard to just let it fail/drop
        // or return the error to the user.

        bool flag = webcraft::net::util::host_port_to_addr(
            info,
            [&](sockaddr *addr, socklen_t len)
            {
                bytes_sent = ::sendto(socket, buffer.data(), (int)buffer.size(), 0, addr, len);
                return bytes_sent >= 0;
            });

        if (!flag)
        {
            // If errno is EAGAIN here, it means the network is saturated.
            // You can choose to throw or return 0.
            std::error_code ec(errno, std::system_category());
            throw std::system_error(ec, "Failed to send data");
        }

        co_return bytes_sent;
    }
    // ---------------------------------------------------------
    // Kqueue Management
    // ---------------------------------------------------------
    void register_with_queue()
    {
        if (socket == -1)
            return;

        // Make socket non-blocking
        int flags = ::fcntl(socket, F_GETFL, 0);
        if (flags == -1)
            throw webcraft::async::detail::macos::kqueue_runtime_error("fcntl get failed");

        if (::fcntl(socket, F_SETFL, flags | O_NONBLOCK) == -1)
            throw webcraft::async::detail::macos::kqueue_runtime_error("fcntl set nonblock failed");

        kq = (int)webcraft::async::detail::get_native_handle();

        // Register READ interest.
        // We do NOT register WRITE interest because you requested synchronous writes.
        struct kevent kev;
        EV_SET(&kev, socket, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, (webcraft::async::detail::runtime_callback *)this);
        if (kevent(kq, &kev, 1, NULL, 0, NULL) == -1)
        {
            throw webcraft::async::detail::macos::kqueue_runtime_error("Could not register UDP kqueue listener");
        }
    }

    void deregister_with_queue()
    {
        if (socket == -1)
            return;

        struct kevent kev;
        EV_SET(&kev, socket, EVFILT_READ, EV_DELETE, 0, 0, NULL);
        // Best effort, ignore errors on close
        kevent(kq, &kev, 1, NULL, 0, NULL);

        // Wake up any pending readers so they can exit/fail
        read_event.notify();
    }

    void try_execute(int result, bool cancelled) override
    {
        if (closed)
            return;

        auto filter = webcraft::async::detail::get_kqueue_filter();

        // When kqueue says there is data, we simply wake up the coroutine.
        // The coroutine (in recvfrom) will perform the actual read.
        if (filter == EVFILT_READ)
        {
            read_event.notify();
        }
    }

    void join_group(const webcraft::async::io::socket::multicast_group &group, const webcraft::async::io::socket::multicast_join_options &) override
    {
        if (socket < 0) return;
        in_addr maddr4;
        if (inet_pton(AF_INET, group.host.c_str(), &maddr4) == 1)
        {
            if (!IN_MULTICAST(ntohl(maddr4.s_addr)))
                throw std::invalid_argument("Not a multicast address: " + group.host);
            struct ip_mreq mreq{};
            mreq.imr_multiaddr = maddr4;
            mreq.imr_interface.s_addr = INADDR_ANY;
            if (setsockopt(socket, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0)
            {
                std::error_code ec(errno, std::system_category());
                throw std::system_error(ec, "Failed to join IPv4 multicast group");
            }
            return;
        }
        struct in6_addr maddr6;
        if (inet_pton(AF_INET6, group.host.c_str(), &maddr6) == 1)
        {
            if (!IN6_IS_ADDR_MULTICAST(&maddr6))
                throw std::invalid_argument("Not a multicast address: " + group.host);
            struct ipv6_mreq mreq6{};
            mreq6.ipv6mr_multiaddr = maddr6;
            mreq6.ipv6mr_interface = 0;
            if (setsockopt(socket, IPPROTO_IPV6, IPV6_JOIN_GROUP, &mreq6, sizeof(mreq6)) < 0)
            {
                std::error_code ec(errno, std::system_category());
                throw std::system_error(ec, "Failed to join IPv6 multicast group");
            }
            return;
        }
        throw std::invalid_argument("Invalid multicast address: " + group.host);
    }

    void leave_group(const webcraft::async::io::socket::multicast_group &group) override
    {
        if (socket < 0) return;
        in_addr maddr4;
        if (inet_pton(AF_INET, group.host.c_str(), &maddr4) == 1)
        {
            struct ip_mreq mreq{};
            mreq.imr_multiaddr = maddr4;
            mreq.imr_interface.s_addr = INADDR_ANY;
            setsockopt(socket, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq, sizeof(mreq));
            return;
        }
        struct in6_addr maddr6;
        if (inet_pton(AF_INET6, group.host.c_str(), &maddr6) == 1)
        {
            struct ipv6_mreq mreq6{};
            mreq6.ipv6mr_multiaddr = maddr6;
            mreq6.ipv6mr_interface = 0;
            setsockopt(socket, IPPROTO_IPV6, IPV6_LEAVE_GROUP, &mreq6, sizeof(mreq6));
        }
    }
};

std::shared_ptr<webcraft::async::io::socket::detail::udp_socket_descriptor> webcraft::async::io::socket::detail::make_udp_socket_descriptor(std::optional<webcraft::async::io::socket::ip_version> version)
{
    return std::make_shared<kqueue_udp_socket_descriptor>(version);
}

#endif