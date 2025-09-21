///////////////////////////////////////////////////////////////////////////////
// Copyright (c) Aditya Rao
// Licenced under MIT license. See LICENSE.txt for details.
///////////////////////////////////////////////////////////////////////////////

#define TEST_SUITE_NAME AsyncNewSocketTestSuite

#include "test_suite.hpp"
#include <webcraft/async/async.hpp>
#include <webcraft/async/io/io.hpp>
#include "mock_io.hpp"

using namespace webcraft::async;
using namespace webcraft::async::io::socket;

const struct connection_info info = {"127.0.0.1", 12345};
const ip_version version = ip_version::IPv4;

TEST_CASE(TestMockUdpWorks)
{
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