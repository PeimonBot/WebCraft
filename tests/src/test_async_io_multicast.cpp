///////////////////////////////////////////////////////////////////////////////
// Copyright (c) Aditya Rao
// Licenced under MIT license. See LICENSE.txt for details.
///////////////////////////////////////////////////////////////////////////////

#define TEST_SUITE_NAME MulticastSocketTestSuite

#ifndef WEBCRAFT_HAS_MULTICAST
#define WEBCRAFT_HAS_MULTICAST 1
#endif

#include "test_suite.hpp"
#include <webcraft/async/async.hpp>
#include <webcraft/async/io/socket.hpp>
#include <atomic>
#include <thread>
#include <chrono>

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

#if WEBCRAFT_HAS_MULTICAST
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
#else
TEST_CASE(TestMulticastJoinLeave)
{
    GTEST_SKIP() << "Multicast not supported (WEBCRAFT_HAS_MULTICAST=0)";
}
#endif

#if WEBCRAFT_HAS_MULTICAST
TEST_CASE(TestMulticastInvalidAddressThrows)
{
    // As of PR #79, resolve() validates and throws std::invalid_argument for non-multicast/invalid addresses.
    EXPECT_THROW(
        (void)multicast_group::resolve("not.an.ip.address"),
        std::invalid_argument);

    // Non-multicast but valid IPv4 (e.g. 192.168.1.1) should also throw from resolve().
    EXPECT_THROW(
        (void)multicast_group::resolve("192.168.1.1"),
        std::invalid_argument);
}
#else
TEST_CASE(TestMulticastInvalidAddressThrows)
{
    GTEST_SKIP() << "Multicast not supported (WEBCRAFT_HAS_MULTICAST=0)";
}
#endif

#if WEBCRAFT_HAS_MULTICAST
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

    auto recv_task = receiver_fn();
    auto send_task = sender_fn();

    // Run receiver in background so it is in recvfrom before we send; then run sender on main thread.
    std::thread recv_thread([&]() { sync_wait(recv_task); });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    sync_wait(send_task);
    recv_thread.join();

    if (!received)
    {
        GTEST_SKIP() << "Multicast loopback not available in this environment (e.g. some macOS CI runners)";
    }
    EXPECT_EQ(received_data, message) << "Received data should match sent message";
}
#else
TEST_CASE(TestMulticastSendReceive)
{
    GTEST_SKIP() << "Multicast not supported (WEBCRAFT_HAS_MULTICAST=0)";
}
#endif

#if WEBCRAFT_HAS_MULTICAST
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
#else
TEST_CASE(TestMulticastJoinLeaveMultipleGroups)
{
    GTEST_SKIP() << "Multicast not supported (WEBCRAFT_HAS_MULTICAST=0)";
}
#endif
