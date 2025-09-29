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
            file = std::fopen(p.c_str(), "r");
        }
        else if (mode & std::ios::out)
        {
            if (mode & std::ios::app)
            {
                file = std::fopen(p.c_str(), "a");
            }
            else
            {
                file = std::fopen(p.c_str(), "w");
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

class iocp_file_descriptor : public file_descriptor
{
protected:
    HANDLE fd;
    HANDLE iocp;
    LONGLONG fileOffset = 0;

public:
    iocp_file_descriptor(std::filesystem::path p, std::ios_base::openmode mode) : file_descriptor(mode)
    {
        // Implementation for Windows
        DWORD desiredAccess = 0;
        DWORD shareMode = FILE_SHARE_READ | FILE_SHARE_WRITE;
        DWORD creationMode = OPEN_EXISTING; // Default for read-only

        if ((mode & std::ios::in) == std::ios::in)
        {
            // Read only
            desiredAccess = GENERIC_READ;
            creationMode = OPEN_EXISTING;
        }
        else if ((mode & std::ios::out) == std::ios::out)
        {
            // Write only
            if ((mode & std::ios::trunc) == std::ios::trunc)
            {
                desiredAccess = GENERIC_WRITE;
                creationMode = CREATE_ALWAYS;
            }
            else if ((mode & std::ios::app) == std::ios::app)
            {
                desiredAccess = FILE_APPEND_DATA;
                creationMode = OPEN_ALWAYS;
            }
            else
            {
                desiredAccess = GENERIC_WRITE;
                creationMode = CREATE_ALWAYS;
            }
        }

        fd = ::CreateFileW(p.c_str(), desiredAccess, shareMode, nullptr, creationMode, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);

        if (fd == INVALID_HANDLE_VALUE)
        {
            throw std::runtime_error("Failed to create file: " + std::to_string(GetLastError()));
        }

        // Associate this file handle with the global IOCP
        iocp = ::CreateIoCompletionPort(fd, (HANDLE)webcraft::async::detail::get_native_handle(), 0, 0);

        if (iocp == nullptr)
        {
            CloseHandle(fd);
            throw std::runtime_error("Failed to associate file with IO completion port: " + std::to_string(GetLastError()));
        }
    }

    ~iocp_file_descriptor()
    {
        if (fd != INVALID_HANDLE_VALUE)
        {
            fire_and_forget(close());
        }
    }

    task<size_t> read(std::span<char> buffer)
    {
        if ((mode & std::ios::in) == std::ios::in)
        {
            LONGLONG offset = fileOffset;
            HANDLE fd = this->fd;
            auto event = webcraft::async::detail::as_awaitable(
                webcraft::async::detail::windows::create_async_io_overlapped_event(
                    fd,
                    [fd, buffer, offset](LPDWORD bytesTransferred, LPOVERLAPPED ptr)
                    {
                        ptr->Offset = static_cast<DWORD>(offset & 0xFFFFFFFF);
                        ptr->OffsetHigh = static_cast<DWORD>((offset >> 32) & 0xFFFFFFFF);
                        return ::ReadFile(fd, buffer.data(), (ULONG)buffer.size(), bytesTransferred, ptr);
                    }));
            co_await event;

            fileOffset += event.get_result();

            co_return event.get_result();
        }
        throw std::ios_base::failure("The file is not opened in read mode");
    }

    task<size_t> write(std::span<char> buffer)
    {
        if ((mode & std::ios::out) == std::ios::out)
        {
            LONGLONG offset = fileOffset;
            HANDLE fd = this->fd;
            auto event = webcraft::async::detail::as_awaitable(
                webcraft::async::detail::windows::create_async_io_overlapped_event(
                    fd,
                    [fd, buffer, offset](LPDWORD bytesTransferred, LPOVERLAPPED ptr)
                    {
                        ptr->Offset = static_cast<DWORD>(offset & 0xFFFFFFFF);
                        ptr->OffsetHigh = static_cast<DWORD>((offset >> 32) & 0xFFFFFFFF);
                        return ::WriteFile(fd, buffer.data(), (ULONG)buffer.size(), bytesTransferred, ptr);
                    }));
            co_await event;

            if (event.get_result() < 0)
            {
                throw std::ios_base::failure("Writing the file failed with error code: " + std::to_string(GetLastError()));
            }

            fileOffset += event.get_result();

            co_return event.get_result();
        }
        throw std::ios_base::failure("The file is not opened in write mode");
    }

    task<void> close()
    {
        if (fd != INVALID_HANDLE_VALUE)
        {
            ::CloseHandle(fd);
            fd = INVALID_HANDLE_VALUE;
        }
        // Don't close iocp as it's the global IOCP handle
        co_return;
    }
};

task<std::shared_ptr<file_descriptor>> webcraft::async::io::fs::detail::make_file_descriptor(std::filesystem::path p, std::ios_base::openmode mode)
{
    co_return std::make_shared<iocp_file_descriptor>(p, mode);
}

#elif defined(__APPLE__)

static webcraft::async::thread_pool pool(std::thread::hardware_concurrency(), std::thread::hardware_concurrency() * 2);

class thread_pool_file_descriptor : public file_descriptor
{
private:
    std::FILE *file;

public:
    thread_pool_file_descriptor(std::filesystem::path p, std::ios_base::openmode mode) : file_descriptor(mode)
    {
        if (mode & std::ios::in)
        {
            file = std::fopen(p.c_str(), "r");
        }
        else if (mode & std::ios::out)
        {
            if (mode & std::ios::app)
            {
                file = std::fopen(p.c_str(), "a");
            }
            else
            {
                file = std::fopen(p.c_str(), "w");
            }
        }
    }

    ~thread_pool_file_descriptor()
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
        task_completion_source<size_t> source;

        pool.submit([&]
                    {
            auto size = std::fread(buffer.data(), sizeof(char), buffer.size(), file);
            source.set_value(size); });

        auto si = co_await source.task();
        co_await yield();
        co_return si;
    }

    task<size_t> write(std::span<char> buffer)
    {

        task_completion_source<size_t> source;

        pool.submit([&]
                    {
            auto size = std::fwrite(buffer.data(), sizeof(char), buffer.size(), file);
            source.set_value(size); });

        auto si = co_await source.task();
        co_await yield();
        co_return si;
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
    co_return std::make_shared<thread_pool_file_descriptor>(p, mode);
}

#else
#endif

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

using on_address_resolved = std::function<bool(sockaddr *addr, socklen_t addrlen)>;

bool host_port_to_addr(const webcraft::async::io::socket::connection_info &info, on_address_resolved callback)
{
    struct addrinfo hints{};
    hints.ai_family = AF_UNSPEC;     // Allow IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM; // TCP

    struct addrinfo *res;
    int ret = getaddrinfo(info.host.c_str(), std::to_string(info.port).c_str(), &hints, &res);
    if (ret != 0)
    {
        return false;
    }

    // Call the callback with the resolved address
    bool check = false;

    for (auto *rp = res; rp; rp = rp->ai_next)
    {
        check = callback(rp->ai_addr, (socklen_t)rp->ai_addrlen);
        if (check)
            break;
        else
            continue;
    }

    freeaddrinfo(res);
    return check;
}

#if defined(WEBCRAFT_MOCK_SOCKET_TESTS)

std::shared_ptr<tcp_socket_descriptor> webcraft::async::io::socket::detail::make_tcp_socket_descriptor()
{
    throw std::runtime_error("TCP socket descriptor not implemented in mock tests");
}

#elif defined(__linux__)

#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <string.h>

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
                ::close(fd);
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

    void shutdown(webcraft::async::io::socket::socket_stream_mode mode) override
    {
        int fd = this->fd;

        if (mode == webcraft::async::io::socket::socket_stream_mode::READ)
        {
            ::shutdown(fd, SHUT_RD);
        }
        else
        {
            ::shutdown(fd, SHUT_WR);
        }
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

std::shared_ptr<webcraft::async::io::socket::detail::tcp_socket_descriptor> webcraft::async::io::socket::detail::make_tcp_socket_descriptor()
{
    return std::make_shared<io_uring_tcp_socket_descriptor>();
}

#elif defined(_WIN32)

struct WSAExtensionManager
{
    LPFN_CONNECTEX ConnectEx;
    LPFN_ACCEPTEX AcceptEx;

    WSAExtensionManager()
    {
        // Initialize function pointers
        ConnectEx = nullptr;
        AcceptEx = nullptr;

        // Get the function pointers
        GUID guidConnectEx = WSAID_CONNECTEX;
        GUID guidAcceptEx = WSAID_ACCEPTEX;
        DWORD bytes;

        SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock != INVALID_SOCKET)
        {
            // Get the ConnectEx function pointer
            if (WSAIoctl(sock, SIO_GET_EXTENSION_FUNCTION_POINTER, &guidConnectEx, sizeof(guidConnectEx), &ConnectEx, sizeof(ConnectEx), &bytes, nullptr, nullptr) != 0)
            {
                throw std::runtime_error("Failed to get ConnectEx function pointer: " + std::to_string(WSAGetLastError()));
            }

            // Get the AcceptEx function pointer
            if (WSAIoctl(sock, SIO_GET_EXTENSION_FUNCTION_POINTER, &guidAcceptEx, sizeof(guidAcceptEx), &AcceptEx, sizeof(AcceptEx), &bytes, nullptr, nullptr) != 0)
            {
                throw std::runtime_error("Failed to get AcceptEx function pointer: " + std::to_string(WSAGetLastError()));
            }

            closesocket(sock);
        }
    }
};

WSAExtensionManager &get_extension_manager()
{
    static WSAExtensionManager manager;
    return manager;
}

BOOL WSAConnectEx(SOCKET s, const sockaddr *name, int namelen, PVOID lpSendBuffer, DWORD dwSendDataLength, LPDWORD lpdwBytesSent, LPOVERLAPPED lpOverlapped)
{
    return get_extension_manager().ConnectEx(s, name, namelen, lpSendBuffer, dwSendDataLength, lpdwBytesSent, lpOverlapped);
}

BOOL WSAAcceptEx(SOCKET sListenSocket, SOCKET sAcceptSocket, PVOID lpOutputBuffer, DWORD dwReceiveDataLength, DWORD dwLocalAddressLength, DWORD dwRemoteAddressLength, LPDWORD lpdwBytesReceived, LPOVERLAPPED lpOverlapped)
{
    return get_extension_manager().AcceptEx(sListenSocket, sAcceptSocket, lpOutputBuffer, dwReceiveDataLength, dwLocalAddressLength, dwRemoteAddressLength, lpdwBytesReceived, lpOverlapped);
}

static webcraft::async::thread_pool pool(std::thread::hardware_concurrency(), std::thread::hardware_concurrency() * 2);

struct iocp_tcp_socket_descriptor : public tcp_socket_descriptor
{
private:
    SOCKET socket;
    HANDLE iocp;
    std::string host;
    uint16_t port;

public:
    iocp_tcp_socket_descriptor() : socket(INVALID_SOCKET), iocp(INVALID_HANDLE_VALUE), port(0)
    {
        get_extension_manager(); // Ensure WSA extensions are loaded
    }

    iocp_tcp_socket_descriptor(SOCKET sock, std::string host, uint16_t port) : socket(sock), iocp(INVALID_HANDLE_VALUE), host(std::move(host)), port(port)
    {
        // Associate this socket handle with the global IOCP
        iocp = ::CreateIoCompletionPort((HANDLE)socket, (HANDLE)webcraft::async::detail::get_native_handle(), 0, 0);

        if (iocp == nullptr)
        {
            ::closesocket(socket);
            throw std::runtime_error("Failed to associate socket with IO completion port: " + std::to_string(GetLastError()));
        }
    }

    ~iocp_tcp_socket_descriptor()
    {
        fire_and_forget(close());
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
            const socklen_t len = (socklen_t)rp->ai_addrlen;

            SOCKET fd = ::socket(family, sock_type, protocol);
            if (fd == INVALID_SOCKET)
            {
                continue;
            }

            iocp = ::CreateIoCompletionPort((HANDLE)fd, (HANDLE)webcraft::async::detail::get_native_handle(), 0, 0);

            if (iocp == nullptr)
            {
                ::closesocket(fd);
                throw std::runtime_error("Failed to associate socket with IO completion port: " + std::to_string(GetLastError()));
            }

            int result = ::connect(fd, res->ai_addr, (int)res->ai_addrlen);
            if (result == SOCKET_ERROR)
            {
                ::closesocket(fd);
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

        co_return;
    }

    task<size_t> read(std::span<char> buffer) override
    {
        SOCKET fd = this->socket;
        WSABUF wsabuf;
        wsabuf.buf = const_cast<char *>(buffer.data());
        wsabuf.len = (ULONG)buffer.size();
        DWORD flags = 0;

        auto event = webcraft::async::detail::as_awaitable(
            webcraft::async::detail::windows::create_async_socket_overlapped_event(
                fd,
                [fd, &wsabuf, &flags](LPDWORD numberOfBytesTransfered, LPOVERLAPPED overlapped)
                {
                    return WSARecv(fd, &wsabuf, 1, numberOfBytesTransfered, &flags, overlapped, nullptr);
                }));

        co_await event;

        co_return event.get_result();
    }

    task<size_t> write(std::span<const char> buffer) override
    {

        SOCKET fd = this->socket;
        WSABUF wsabuf;
        wsabuf.buf = const_cast<char *>(buffer.data());
        wsabuf.len = (ULONG)buffer.size();

        auto event = webcraft::async::detail::as_awaitable(
            webcraft::async::detail::windows::create_async_socket_overlapped_event(
                fd,
                [fd, &wsabuf](LPDWORD numberOfBytesTransfered, LPWSAOVERLAPPED overlapped)
                {
                    return WSASend(fd, &wsabuf, 1, numberOfBytesTransfered, 0, overlapped, nullptr);
                }));

        co_await event;

        co_return event.get_result();
    }

    void shutdown(webcraft::async::io::socket::socket_stream_mode mode)
    {
        if (mode == webcraft::async::io::socket::socket_stream_mode::READ)
        {
            ::shutdown(socket, SD_RECEIVE);
        }
        else
        {
            ::shutdown(socket, SD_SEND);
        }
    }

    std::string get_remote_host() override
    {
        return host;
    }

    uint16_t get_remote_port() override
    {
        return port;
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

std::shared_ptr<tcp_socket_descriptor> webcraft::async::io::socket::detail::make_tcp_socket_descriptor()
{
    return std::make_shared<iocp_tcp_socket_descriptor>();
}

#elif defined(__APPLE__)

#include <fcntl.h>

class async_single_resumer_latch
{
    std::optional<std::coroutine_handle<>> handle;
    std::string event_name;

public:
    explicit async_single_resumer_latch(std::string event_name) : event_name(event_name), handle(std::nullopt) {}

    constexpr bool await_ready() { return false; }
    void await_suspend(std::coroutine_handle<> h)
    {
        // std::cout << "Setting the handle value to wait on." << std::endl;
        this->handle = h;
    }
    constexpr void await_resume()
    {
        handle = std::nullopt;
    }

    void notify()
    {
        if (handle.has_value() && handle.value() && !handle.value().done())
        {
            // std::cout << "Performing resume: " << event_name << std::endl;
            handle.value().resume();
            // std::cout << "Resume performed: " << event_name << std::endl;
        }
    }
};

class kqueue_tcp_socket_descriptor : public tcp_socket_descriptor,
                                     public webcraft::async::detail::runtime_callback
{
private:
    int fd;
    std::atomic<bool> closed{false};

    std::string host;
    uint16_t port;

    int kq;

    std::vector<char> read_buffer;
    std::vector<char> write_buffer;
    async_single_resumer_latch read_event{"read_event"};
    async_single_resumer_latch write_event{"write_event"};
    bool no_more_bytes{false};

public:
    kqueue_tcp_socket_descriptor()
    {
        fd = -1;
    }

    kqueue_tcp_socket_descriptor(int fd, std::string host, uint16_t port) : fd(fd), host(std::move(host)), port(port)
    {
        register_with_queue();
    }

    ~kqueue_tcp_socket_descriptor()
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
            deregister_with_queue();
            ::close(fd);
        }
    }

    task<size_t> read(std::span<char> buffer) override
    {
        if (no_more_bytes)
            co_return 0;

        if (buffer.size() <= read_buffer.size())
        {
            std::copy(read_buffer.begin(), read_buffer.begin() + buffer.size(), buffer.begin());

            // remove the read portions of the read buffer
            read_buffer.erase(read_buffer.begin(), read_buffer.begin() + buffer.size());
            co_return buffer.size();
        }

        std::cout << "Taking a nap til event wakes me" << std::endl;
        co_await read_event;
        std::cout << "Awoken" << std::endl;

        if (no_more_bytes)
            co_return 0;

        // copy what can fit into the buffer
        std::cout << "Copy what can fit" << std::endl;
        auto min_read = std::min(buffer.size(), read_buffer.size());
        std::copy(read_buffer.begin(), read_buffer.begin() + min_read, buffer.begin());

        std::cout << "Data copied" << std::endl;
        // remove the read portions of the read buffer
        read_buffer.erase(read_buffer.begin(), read_buffer.begin() + min_read);
        std::cout << "Data offset" << std::endl;

        co_return min_read;
    }

    task<size_t> write(std::span<const char> buffer) override
    {
        write_buffer.insert(write_buffer.end(), buffer.begin(), buffer.end());

        // drain the write buffer
        while (write_buffer.size() > 0)
            co_await write_event;

        // Look at the difference in size of the buffer
        co_return buffer.size();
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
            if (::connect(fd, rp->ai_addr, rp->ai_addrlen) < 0)
            {
                ::close(fd);
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

        register_with_queue();
        co_return;
    }

    void shutdown(webcraft::async::io::socket::socket_stream_mode mode) override
    {
        int fd = this->fd;

        if (mode == webcraft::async::io::socket::socket_stream_mode::READ)
        {
            ::shutdown(fd, SHUT_RD);
        }
        else
        {
            ::shutdown(fd, SHUT_WR);
        }
    }

    std::string get_remote_host() override
    {
        return host;
    }

    uint16_t get_remote_port() override
    {
        return port;
    }

    void register_with_queue()
    {
        // make socket non-blocking
        int flags = ::fcntl(fd, F_GETFL, 0);
        if (flags == -1)
        {
            throw std::runtime_error("Error in getting socket flags");
        }
        int res = ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        if (res == -1)
        {
            throw std::runtime_error("Error in setting non-blocking flag");
        }

        kq = (int)webcraft::async::detail::get_native_handle();

        struct kevent kev;
        EV_SET(&kev, fd, EVFILT_READ, EV_ADD, 0, 0, (webcraft::async::detail::runtime_callback *)this);
        if (kevent(kq, &kev, 1, NULL, 0, NULL) == -1)
        {
            throw std::runtime_error("Could not register read listener");
        }

        EV_SET(&kev, fd, EVFILT_WRITE, EV_ADD, 0, 0, (webcraft::async::detail::runtime_callback *)this);
        if (kevent(kq, &kev, 1, NULL, 0, NULL) == -1)
        {
            throw std::runtime_error("Could not register write listener");
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

        EV_SET(&kev, fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
        if (kevent(kq, &kev, 1, NULL, 0, NULL) == -1)
        {
            std::cerr << "Could not unregister listener" << std::endl;
        }
    }

    void try_execute(int result, bool cancelled) override
    {
        auto filter = webcraft::async::detail::get_kqueue_filter();
        auto flags = webcraft::async::detail::get_kqueue_flags();

        if (flags & EV_EOF)
        {
            std::cout << "Notify that no more reads are going to be arriving" << std::endl;
            no_more_bytes = true;
            read_event.notify();
        }

        // std::cout << "Getting some signals: " << filter << " " << flags << std::endl;

        if (filter == EVFILT_READ)
        {
            std::array<char, 1024> buffer{};
            std::cout << "Performing read on read buffer" << std::endl;
            while (true)
            {
                std::cout << "Reading some bytes" << std::endl;
                int bytes_read = ::recv(fd, buffer.data(), buffer.size(), 0);
                std::cout << "Bytes read: " << bytes_read << std::endl;
                if (bytes_read < 0)
                {
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                        break;
                    else
                        throw std::runtime_error("This should not have happened but read failed: " + std::to_string(errno));
                }

                std::cout << "Dumping bytes into buffer" << std::endl;
                read_buffer.insert(read_buffer.end(), buffer.begin(), buffer.begin() + bytes_read);
            }
            std::cout << "Done reading all available bytes. Read buffer grew to " << read_buffer.size() << std::endl;
            if (read_buffer[0] == '\0')
            {
                std::cout << "Something went wrong while reading" << std::endl;
            }
            read_event.notify();
        }

        if (filter == EVFILT_WRITE)
        {
            if (!write_buffer.empty())
            {
                int bytes_written = ::send(fd, write_buffer.data(), write_buffer.size(), 0);
                write_buffer.erase(write_buffer.begin(), write_buffer.begin() + bytes_written);
                write_event.notify();
            }
        }
    }
};

std::shared_ptr<webcraft::async::io::socket::detail::tcp_socket_descriptor> webcraft::async::io::socket::detail::make_tcp_socket_descriptor()
{
    return std::make_shared<kqueue_tcp_socket_descriptor>();
}

#endif

#ifdef WEBCRAFT_MOCK_LISTENER_TESTS

std::shared_ptr<webcraft::async::io::socket::detail::tcp_listener_descriptor> webcraft::async::io::socket::detail::make_tcp_listener_descriptor()
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

        auto [host, port] = addr_to_host_port(addr);

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

        SOCKET fd = this->socket;

        task_completion_source<SOCKET> accept_socket_source;
        struct sockaddr_storage addr;
        socklen_t addr_len = sizeof(addr);

        pool.submit([fd, &addr, &addr_len, &accept_socket_source]
                    {
            SOCKET result = ::accept(fd, (struct sockaddr *)&addr, &addr_len);
            accept_socket_source.set_value(result); });

        SOCKET acceptSocket = co_await accept_socket_source.task();

        if (acceptSocket == INVALID_SOCKET)
        {
            co_return nullptr;
        }

        auto [remote_host, remote_port] = addr_to_host_port(addr);

        co_return std::make_shared<iocp_tcp_socket_descriptor>(acceptSocket, remote_host, remote_port);
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
    async_event read_ev;
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

        co_await read_ev;

        int fd = this->fd;
        struct sockaddr_storage addr;
        socklen_t addr_len = sizeof(addr);

        int result = ::accept(fd, (struct sockaddr *)&addr, (socklen_t *)&addr_len);

        if (result < 0)
        {
            throw std::ios_base::failure("Failed to accept connection: " + std::to_string(result) + ", value: " + strerror(errno));
        }

        auto [host, port] = addr_to_host_port(addr);

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
    }

    void try_execute(int result, bool cancelled) override
    {
        auto filter = webcraft::async::detail::get_kqueue_filter();
        auto flags = webcraft::async::detail::get_kqueue_flags();

        if (filter == EVFILT_READ)
        {
            read_ev.set();
        }
    }
};

std::shared_ptr<webcraft::async::io::socket::detail::tcp_listener_descriptor> webcraft::async::io::socket::detail::make_tcp_listener_descriptor()
{
    return std::make_shared<kqueue_tcp_listener_descriptor>();
}

#endif

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
                if (socket < 0)
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
        std::cout << "Bound to socket " << socket << std::endl;

        if (!flag)
            throw std::ios_base::failure("Failed to create socket: " + std::string(get_error_string(get_last_socket_error())));
    }

    task<size_t> recvfrom(std::span<char> buffer, webcraft::async::io::socket::connection_info &info) override
    {
        sockaddr_storage addr;
        socklen_t addr_len = sizeof(addr);
        auto size = ::recvfrom(socket, buffer.data(), (int)buffer.size(), 0, (sockaddr *)&addr, &addr_len);
        if (size < 0)
        {
            throw std::ios_base::failure("Failed to receive data: " + std::string(get_error_string(get_last_socket_error())));
        }

        // Extract connection info
        auto [host, port] = addr_to_host_port(addr);

        info.host = host;
        info.port = port;

        co_return size;
    }

    task<size_t> sendto(std::span<const char> buffer, const webcraft::async::io::socket::connection_info &info) override
    {
        SOCKET socket = this->socket;
        int bytes_sent = -1;
        bool flag = host_port_to_addr(
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
#elif defined(__linux__)
#elif defined(__APPLE__)
#endif