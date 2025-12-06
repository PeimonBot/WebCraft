///////////////////////////////////////////////////////////////////////////////
// Copyright (c) Aditya Rao
// Licenced under MIT license. See LICENSE.txt for details.
///////////////////////////////////////////////////////////////////////////////

#pragma once

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

#if defined(WEBCRAFT_MOCK_SOCKET_TESTS)
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
    io_uring_tcp_socket_descriptor();

    io_uring_tcp_socket_descriptor(int fd, std::string host, uint16_t port);

    ~io_uring_tcp_socket_descriptor();

    task<void> close();

    task<size_t> read(std::span<char> buffer) override;

    task<size_t> write(std::span<const char> buffer) override;

    task<void> connect(const webcraft::async::io::socket::connection_info &info) override;

    void shutdown(webcraft::async::io::socket::socket_stream_mode mode) override;

    std::string get_remote_host() override;

    uint16_t get_remote_port() override;
};

#elif defined(_WIN32)

struct WSAExtensionManager
{
    LPFN_CONNECTEX ConnectEx;
    LPFN_ACCEPTEX AcceptEx;

    explicit WSAExtensionManager()
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

inline WSAExtensionManager &get_extension_manager()
{
    static WSAExtensionManager manager;
    return manager;
}

struct iocp_tcp_socket_descriptor : public tcp_socket_descriptor
{
private:
    SOCKET socket;
    HANDLE iocp;
    std::string host;
    uint16_t port;

public:
    iocp_tcp_socket_descriptor();

    iocp_tcp_socket_descriptor(SOCKET sock, std::string host, uint16_t port);

    ~iocp_tcp_socket_descriptor();

    task<void> connect(const webcraft::async::io::socket::connection_info &info) override;

    task<size_t> read(std::span<char> buffer) override;

    task<size_t> write(std::span<const char> buffer) override;

    void shutdown(webcraft::async::io::socket::socket_stream_mode mode);

    std::string get_remote_host();

    uint16_t get_remote_port();

    task<void> close();
};

#elif defined(__APPLE__)

#include <fcntl.h>

class async_single_resumer_latch
{
    std::optional<std::coroutine_handle<>> handle;

public:
    explicit async_single_resumer_latch() : handle(std::nullopt) {}

    constexpr bool await_ready() { return false; }
    inline void await_suspend(std::coroutine_handle<> h)
    {
        this->handle = h;
    }
    constexpr inline void await_resume()
    {
        handle = std::nullopt;
    }

    inline void notify()
    {
        if (handle.has_value() && handle.value() && !handle.value().done())
        {
            handle.value().resume();
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
    async_single_resumer_latch read_event{};
    async_single_resumer_latch write_event{};
    bool no_more_bytes{false};

public:
    kqueue_tcp_socket_descriptor();

    kqueue_tcp_socket_descriptor(int fd, std::string host, uint16_t port);

    ~kqueue_tcp_socket_descriptor();

    task<void> close() override;

    task<size_t> read(std::span<char> buffer) override;

    task<size_t> write(std::span<const char> buffer) override;

    task<void> connect(const webcraft::async::io::socket::connection_info &info) override;

    void shutdown(webcraft::async::io::socket::socket_stream_mode mode) override;

    std::string get_remote_host() override;

    uint16_t get_remote_port() override;

    void register_with_queue();

    void deregister_with_queue();

    void try_execute(int result, bool cancelled) override;
};

#endif