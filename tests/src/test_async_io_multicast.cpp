///////////////////////////////////////////////////////////////////////////////
// Copyright (c) Aditya Rao
// Licenced under MIT license. See LICENSE.txt for details.
///////////////////////////////////////////////////////////////////////////////

#define TEST_SUITE_NAME MulticastSocketTestSuite

#include "test_suite.hpp"
#include <webcraft/async/async.hpp>
#include <webcraft/async/io/socket.hpp>
#include <atomic>

using namespace webcraft::async;
using namespace webcraft::async::io::socket;

namespace
{
    constexpr uint16_t multicast_port = 19000;
    const std::string multicast_addr = "239.255.0.1";
    const connection_info bind_info = {"0.0.0.0", multicast_port};
    const ip_version version = ip_version::IPv4;
} // namespace

TEST_CASE(TestMulticastGroupResolve)
{
    auto group = multicast_group::resolve(multicast_addr);
    EXPECT_EQ(group.host, multicast_addr);
    EXPECT_EQ(group.port, 0u);

    group.port = multicast_port;
    EXPECT_EQ(group.port, multicast_port);
}

TEST_CASE(TestMulticastJoinLeave)
{
    runtime_context context;

    auto task_fn = co_async
    {
        multicast_socket socket = make_multicast_socket(version);
        socket.bind(bind_info);

        multicast_group group = multicast_group::resolve(multicast_addr);
        group.port = multicast_port;

        EXPECT_NO_THROW(socket.join(group));
        EXPECT_NO_THROW(socket.leave(group));

        co_await socket.close();
    };

    sync_wait(task_fn());
}

TEST_CASE(TestMulticastInvalidAddressThrows)
{
    runtime_context context;

    auto task_fn = co_async
    {
        multicast_socket socket = make_multicast_socket(version);
        socket.bind(bind_info);

        // Resolve with an invalid/non-multicast string; join may throw std::invalid_argument or std::system_error
        multicast_group invalid_group = multicast_group::resolve("not.an.ip.address");
        invalid_group.port = multicast_port;

#if !defined(WEBCRAFT_UDP_MOCK)
        EXPECT_THROW(socket.join(invalid_group), std::exception);
#endif

        co_await socket.close();
    };

    sync_wait(task_fn());
}

TEST_CASE(TestMulticastSendReceive)
{
    runtime_context context;

    multicast_group group = multicast_group::resolve(multicast_addr);
    group.port = multicast_port;

    const std::string message = "Hello, multicast!";
    std::atomic<bool> received{false};
    std::string received_data;

    auto receiver_fn = co_async
    {
        multicast_socket recv_socket = make_multicast_socket(version);
        recv_socket.bind(bind_info);
        recv_socket.join(group);

        std::vector<char> buffer(1024);
        connection_info sender_info{};
        size_t n = co_await recv_socket.recvfrom(std::span<char>(buffer.data(), buffer.size()), sender_info);
        if (n > 0)
        {
            received_data.assign(buffer.data(), n);
            received = true;
        }

        recv_socket.leave(group);
        co_await recv_socket.close();
    };

    auto sender_fn = co_async
    {
        multicast_socket send_socket = make_multicast_socket(version);
        size_t n = co_await send_socket.sendto(std::span<const char>(message.data(), message.size()), group);
        EXPECT_EQ(n, message.size());
        co_await send_socket.close();
    };

    // Start receiver first, then sender
    auto recv_task = receiver_fn();
    auto send_task = sender_fn();

    sync_wait(send_task);
    sync_wait(recv_task);

    EXPECT_TRUE(received) << "Receiver should have received multicast data";
    EXPECT_EQ(received_data, message) << "Received data should match sent message";
}

TEST_CASE(TestMulticastJoinLeaveMultipleGroups)
{
    runtime_context context;

    auto task_fn = co_async
    {
        multicast_socket socket = make_multicast_socket(version);
        socket.bind(bind_info);

        auto group1 = multicast_group::resolve("239.255.0.1");
        group1.port = multicast_port;
        auto group2 = multicast_group::resolve("239.255.0.2");
        group2.port = multicast_port;

        socket.join(group1);
        socket.join(group2);
        socket.leave(group1);
        socket.leave(group2);

        co_await socket.close();
    };

    sync_wait(task_fn());
}
