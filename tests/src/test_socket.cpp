
#define TEST_SUITE_NAME SocketTestSuite

#include "test_suite.hpp"
#include <string>
#include <set>
#include <list>
#include <vector>
#include <webcraft/async/async.hpp>
#include <webcraft/async/io/socket.hpp>

using namespace webcraft::async::io::socket;
using namespace webcraft::async;

TEST_CASE(TestMockCreateTcpSocket)
{
    auto fn = []() -> task<void>
    {
        auto socket = make_tcp_socket("localhost", 8080);
        co_await socket.connect();
        auto &rstream = socket.get_readable_stream();
        auto &wstream = socket.get_writable_stream();

        std::string message = "Hello, World!";
        size_t bytes_written = co_await wstream.send(std::span<const char>(message.data(), message.size()));
        EXPECT_EQ(bytes_written, message.size());

        std::array<char, 1024> buffer;
        size_t bytes_read = co_await rstream.recv(std::span<char>(buffer));
        EXPECT_GE(bytes_read, 0);
    };

    sync_wait(fn());
}

TEST_CASE(TestMockCreateTcpListener)
{
    auto fn = []() -> task<void>
    {
        auto listener = make_tcp_listener("localhost", 8080);
        listener.listen(0);

        auto client_socket = co_await listener.accept();
        co_await client_socket.connect();

        auto &rstream = client_socket.get_readable_stream();
        auto &wstream = client_socket.get_writable_stream();

        std::string message = "Hello, Client!";
        size_t bytes_written = co_await wstream.send(std::span<const char>(message.data(), message.size()));
        EXPECT_EQ(bytes_written, message.size());

        std::array<char, 1024> buffer;
        size_t bytes_read = co_await rstream.recv(std::span<char>(buffer));
        EXPECT_GE(bytes_read, 0);
    };

    sync_wait(fn());
}