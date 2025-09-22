///////////////////////////////////////////////////////////////////////////////
// Copyright (c) Aditya Rao
// Licenced under MIT license. See LICENSE.txt for details.
///////////////////////////////////////////////////////////////////////////////

#define TEST_SUITE_NAME AsyncNewSocketTestSuite

#include "test_suite.hpp"
#include <webcraft/async/async.hpp>
#include "mock_io.hpp"

using namespace webcraft::async;
using namespace webcraft::async::io::socket;

const struct connection_info info = {"127.0.0.1", 12345};
const ip_version version = ip_version::IPv4;

TEST_CASE(TestMockUdpWorks)
{
    runtime_context context;
    std::cout << "Starting UDP Echo Server on " << info.host << ":" << info.port << std::endl;
    webcraft::test::udp::echo_server server(info);
    std::cout << "Creating UDP Echo Client" << std::endl;
    webcraft::test::udp::echo_client client(version);

    const std::string message = "Hello, UDP Echo!";

    for (size_t i = 0; i < 5; ++i)
    {
        std::cout << "UDP Echo Attempt " << (i + 1) << std::endl;
        bool success = client.echo(message, info);
        EXPECT_TRUE(success) << "UDP Echo should succeeded on the " << (i + 1) << " attempt";
    }

    std::cout << "Closing UDP Echo Client and Server" << std::endl;
    client.close();
    std::cout << "Client closed" << std::endl;
    server.close();
}

TEST_CASE(TestMockTcpWorks)
{
    runtime_context context;
    std::cout << "Starting TCP Echo Server on " << info.host << ":" << info.port << std::endl;
    webcraft::test::tcp::echo_server server(info);
    std::cout << "Creating TCP Echo Client for server at " << info.host << ":" << info.port << std::endl;
    webcraft::test::tcp::echo_client client(info);

    const std::string message = "Hello, TCP Echo!";

    for (size_t i = 0; i < 5; ++i)
    {
        std::cout << "TCP Echo Attempt " << (i + 1) << std::endl;
        bool success = client.echo(message);
        EXPECT_TRUE(success) << "TCP Echo should succeeded on the " << (i + 1) << " attempt";
    }

    std::cout << "Closing TCP Echo Client and Server" << std::endl;
    client.close();
    std::cout << "Client closed" << std::endl;
    server.close();
}

class async_tcp_echo_client
{
private:
    tcp_socket socket;

public:
    async_tcp_echo_client() : socket(make_tcp_socket()) {}

    task<void> close()
    {
        co_await socket.close();
    }

    task<void> connect(const connection_info &info)
    {
        co_await socket.connect(info);
    }

    task<bool> echo(const std::string &message)
    {
        auto &writer = socket.get_writable_stream();
        auto &reader = socket.get_readable_stream();

        size_t bytes_sent = co_await writer.send(std::span<const char>(message.data(), message.size()));
        if (bytes_sent != message.size())
        {
            co_return false; // Error sending data
        }

        std::vector<char> buffer(message.size());
        size_t bytes_received = co_await reader.recv(std::span<char>(buffer.data(), buffer.size()));
        if (bytes_received != message.size())
        {
            co_return false; // Error receiving data
        }

        co_return std::string(buffer.data(), buffer.size()) == message; // Check if the received message matches the sent message
    }
};

TEST_CASE(TestAsyncTcpSocket)
{
    runtime_context context;
    std::cout << "Starting TCP Echo Server on " << info.host << ":" << info.port << std::endl;
    webcraft::test::tcp::echo_server server(info);
    std::cout << "Creating Async TCP Socket for server at " << info.host << ":" << info.port << std::endl;

    auto task_fn = co_async
    {
        async_tcp_echo_client client;
        co_await client.connect(info);
        const std::string message = "Hello, Async TCP Echo!";

        for (size_t i = 0; i < 5; ++i)
        {
            std::cout << "Async TCP Echo Attempt " << (i + 1) << std::endl;
            bool success = co_await client.echo(message);
            EXPECT_TRUE(success) << "Async TCP Echo should succeeded on the " << (i + 1) << " attempt";
        }

        std::cout << "Closing Async TCP Echo Client" << std::endl;
        co_await client.close();
    };

    sync_wait(task_fn());

    std::cout << "Closing TCP Echo Server" << std::endl;
    server.close();
}