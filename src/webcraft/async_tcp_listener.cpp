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
#include "async_tcp_socket_decl.hpp"

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

#if defined(__linux__)

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
            this->fd = -1;
        }
        co_return;
    }

    void bind(const webcraft::async::io::socket::connection_info &info) override
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
    }

    void listen(int backlog) override
    {
        int fd = this->fd;

        int result = ::listen(fd, backlog);

        if (result < 0)
        {
            throw std::ios_base::failure("Failed to listen: " + std::to_string(result) + ", value: " + strerror(errno));
        }
    }

    task<std::shared_ptr<tcp_socket_descriptor>> accept() override
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

        auto [host, port] = webcraft::net::util::addr_to_host_port(addr);

        if (host.empty() || port == 0)
        {
            co_return nullptr;
        }

        co_return std::make_shared<io_uring_tcp_socket_descriptor>(event.get_result(), host, port);
    }
};

std::shared_ptr<webcraft::async::io::socket::detail::tcp_listener_descriptor> webcraft::async::io::socket::detail::make_tcp_listener_descriptor()
{
    return std::make_shared<io_uring_tcp_listener_descriptor>();
}

#elif defined(_WIN32)

class iocp_tcp_socket_listener : public tcp_listener_descriptor
{
private:
    SOCKET socket;
    HANDLE iocp;
    std::atomic<bool> closed{false};
    int family; // Store the address family (AF_INET/AF_INET6)

public:
    iocp_tcp_socket_listener() : socket(INVALID_SOCKET), iocp(INVALID_HANDLE_VALUE)
    {
        get_extension_manager(); // Ensure WSA extensions are loaded
    }

    ~iocp_tcp_socket_listener()
    {
        fire_and_forget(close());
    }

    void bind(const webcraft::async::io::socket::connection_info &info) override
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
            socklen_t len = (socklen_t)rp->ai_addrlen;

            SOCKET fd = ::socket(family, sock_type, protocol);
            if (fd == INVALID_SOCKET)
            {
                continue;
            }

            iocp = CreateIoCompletionPort((HANDLE)fd, (HANDLE)webcraft::async::detail::get_native_handle(), 0, 0);
            if (iocp == INVALID_HANDLE_VALUE)
            {
                ::closesocket(fd);
                continue;
            }

            // Await io_uring bind
            int result = ::bind(fd, addr, len);

            if (result < 0)
            {
                ::closesocket(fd);
                continue;
            }
            else
            {

                this->socket = fd;
                this->family = family;
                flag = true;
                break;
            }
        }

        freeaddrinfo(res); // Free memory allocated by getaddrinfo

        if (!flag)
            throw std::ios_base::failure("Failed to create socket: " + std::to_string(WSAGetLastError()));
    }

    void listen(int backlog) override
    {
        SOCKET fd = this->socket;

        int result = ::listen(fd, backlog);

        if (result < 0)
        {
            throw std::ios_base::failure("Failed to listen: " + std::to_string(result) + ", value: " + std::to_string(WSAGetLastError()));
        }
    }

    task<std::shared_ptr<tcp_socket_descriptor>> accept() override
    {
        if (socket == INVALID_SOCKET)
            co_return nullptr;

        SOCKET listen_socket = this->socket;

        // 1. Pre-create the socket that will become the accepted client.
        // It must match the family (IPv4/IPv6) of the listener.
        SOCKET accept_socket = ::socket(this->family, SOCK_STREAM, IPPROTO_TCP);
        if (accept_socket == INVALID_SOCKET)
        {
            throw std::ios_base::failure("Failed to create accept socket: " + std::to_string(WSAGetLastError()));
        }

        // 2. Prepare the buffer.
        // AcceptEx requires: (sizeof(sockaddr_storage) + 16) for *each* address (local and remote).
        // We aren't reading initial data, so the data buffer size is 0.
        const DWORD addr_len = sizeof(sockaddr_storage) + 16;
        const DWORD buffer_size = addr_len * 2;

        // We keep this buffer alive in the coroutine frame until co_await returns
        std::vector<char> output_buffer(buffer_size);

        auto event = webcraft::async::detail::as_awaitable(
            webcraft::async::detail::windows::create_async_socket_overlapped_event(
                listen_socket,
                [listen_socket, accept_socket, &output_buffer, buffer_size, addr_len](LPDWORD bytesReceived, LPOVERLAPPED overlapped)
                {
                    // 3. Call AcceptEx
                    // Note: We pass 0 for dwReceiveDataLength because we only want to accept the connection,
                    // we don't want to wait for the first bytes of data (which can open DoS vulnerabilities).
                    return get_extension_manager().AcceptEx(
                        listen_socket,
                        accept_socket,
                        output_buffer.data(),
                        0,        // dwReceiveDataLength
                        addr_len, // dwLocalAddressLength
                        addr_len, // dwRemoteAddressLength
                        bytesReceived,
                        overlapped);
                }));

        // 4. Wait for the IOCP completion
        co_await event;

        // If the socket was closed while waiting, cleanup
        if (socket == INVALID_SOCKET || closed)
        {
            ::closesocket(accept_socket);
            co_return nullptr;
        }

        // 5. Context Update (CRITICAL STEP)
        // Without this, SO_KEEPALIVE, SO_LINGER, etc., are broken on the new socket.
        if (setsockopt(accept_socket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
                       (char *)&listen_socket, sizeof(listen_socket)) != 0)
        {
            ::closesocket(accept_socket);
            throw std::ios_base::failure("Failed to update accept context: " + std::to_string(WSAGetLastError()));
        }

        // 6. Parse addresses to get the Remote Host and Port
        sockaddr *local_addr = nullptr;
        sockaddr *remote_addr = nullptr;
        int local_addr_len = 0;
        int remote_addr_len = 0;

        GetAcceptExSockaddrs(
            output_buffer.data(),
            0, // dwReceiveDataLength must match what was passed to AcceptEx
            addr_len,
            addr_len,
            &local_addr,
            &local_addr_len,
            &remote_addr,
            &remote_addr_len);

        // Convert parsed sockaddr to our helper struct
        // (Assuming you have a helper that takes sockaddr*)
        sockaddr_storage remote_storage{};
        std::memcpy(&remote_storage, remote_addr, remote_addr_len);

        auto [remote_host, remote_port] = webcraft::net::util::addr_to_host_port(remote_storage);

        // 7. Return the descriptor
        // The iocp_tcp_socket_descriptor constructor will call CreateIoCompletionPort
        // to associate this new 'accept_socket' with the global IOCP handle.
        co_return std::make_shared<iocp_tcp_socket_descriptor>(accept_socket, remote_host, remote_port);
    }

    task<void> close() override
    {
        if (socket != INVALID_SOCKET)
        {
            ::closesocket(socket);
            socket = INVALID_SOCKET;
        }

        // Don't close iocp as it's the global IOCP handle
        co_return;
    }
};

std::shared_ptr<webcraft::async::io::socket::detail::tcp_listener_descriptor> webcraft::async::io::socket::detail::make_tcp_listener_descriptor()
{
    return std::make_shared<iocp_tcp_socket_listener>();
}

#elif defined(__APPLE__)

class kqueue_tcp_listener_descriptor : public webcraft::async::io::socket::detail::tcp_listener_descriptor, public webcraft::async::detail::runtime_callback
{
private:
    int fd;
    std::atomic<bool> closed{false};
    bool no_more_connections{false};
    async_single_resumer_latch read_event{"read_event"};
    int kq;

public:
    kqueue_tcp_listener_descriptor()
    {
        fd = -1;
    }

    ~kqueue_tcp_listener_descriptor()
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

            ::close(fd);
            deregister_with_queue();
            this->fd = -1;
        }
        co_return;
    }

    void bind(const webcraft::async::io::socket::connection_info &info) override
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
    }

    void listen(int backlog) override
    {
        int fd = this->fd;

        int result = ::listen(fd, backlog);

        if (result < 0)
        {
            throw std::ios_base::failure("Failed to listen: " + std::to_string(result) + ", value: " + strerror(errno));
        }

        register_with_queue();
    }

    task<std::shared_ptr<tcp_socket_descriptor>> accept() override
    {
        if (no_more_connections)
            co_return nullptr;

        co_await read_event;

        if (no_more_connections)
            co_return nullptr;

        int fd = this->fd;
        struct sockaddr_storage addr;
        socklen_t addr_len = sizeof(addr);

        int result = ::accept(fd, (struct sockaddr *)&addr, (socklen_t *)&addr_len);

        if (result < 0)
        {
            throw std::ios_base::failure("Failed to accept connection: " + std::to_string(result) + ", value: " + strerror(errno));
        }

        auto [host, port] = webcraft::net::util::addr_to_host_port(addr);

        if (host.empty() || port == 0)
        {
            co_return nullptr;
        }

        co_return std::make_shared<kqueue_tcp_socket_descriptor>(result, host, port);
    }

    void register_with_queue()
    {
        kq = (int)webcraft::async::detail::get_native_handle();

        struct kevent kev;
        EV_SET(&kev, fd, EVFILT_READ, EV_ADD, 0, 0, (webcraft::async::detail::runtime_callback *)this);
        if (kevent(kq, &kev, 1, NULL, 0, NULL) == -1)
        {
            throw std::runtime_error("Could not register read listener");
        }
    }

    void deregister_with_queue()
    {
        struct kevent kev;
        EV_SET(&kev, fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
        if (kevent(kq, &kev, 1, NULL, 0, NULL) == -1)
        {
            std::cerr << "Could not unregister listener" << std::endl;
        }

        no_more_connections = true;
        read_event.notify();
    }

    void try_execute(int result, bool cancelled) override
    {
        if (closed)
            return;
        auto filter = webcraft::async::detail::get_kqueue_filter();
        auto flags = webcraft::async::detail::get_kqueue_flags();

        if (filter == EVFILT_READ)
        {
            if (no_more_connections)
                return;

            read_event.notify();
        }
    }
};

std::shared_ptr<webcraft::async::io::socket::detail::tcp_listener_descriptor> webcraft::async::io::socket::detail::make_tcp_listener_descriptor()
{
    return std::make_shared<kqueue_tcp_listener_descriptor>();
}

#endif
