#define TEST_SUITE_NAME AsyncSocketTestSuite

#include "test_suite.hpp"
#include <webcraft/async/async.hpp>
#include <webcraft/async/io/io.hpp>
#include <filesystem>
#include <sstream>

using namespace webcraft::async;
using namespace webcraft::async::io;
using namespace webcraft::async::io::adaptors;
using namespace webcraft::async::io::socket;

const std::string host = "google.com";
const uint16_t port = 80;

struct connection_results
{
    std::string response;
};

connection_results get_google_results_sync();

std::string get_body(const std::string &response)
{
    auto pos = response.find("\r\n\r\n");
    return (pos != std::string::npos) ? response.substr(pos + 4) : response;
}

std::string get_status_line(const std::string &resp)
{
    auto pos = resp.find("\r\n");
    return (pos != std::string::npos) ? resp.substr(0, pos) : resp;
};

TEST_CASE(TestInternetAvailable)
{
    connection_results results;
    EXPECT_NO_THROW(results = get_google_results_sync()) << "Internet should be available";

    EXPECT_FALSE(results.response.empty()) << "Response should not be empty";
}

task<connection_results> get_google_results_async(tcp_rstream &rstream, tcp_wstream &wstream);

TEST_CASE(TestSocketConnection)
{
    runtime_context context;

    connection_results sync_results = get_google_results_sync();

    auto task_fn = [&]() -> task<void>
    {
        auto socket = co_await make_tcp_socket();

        co_await socket.connect({host, port});

        EXPECT_EQ(host, socket.get_remote_host()) << "Remote host should match";
        EXPECT_EQ(port, socket.get_remote_port()) << "Remote port should match";

        auto &socket_rstream = socket.get_readable_stream();
        auto &socket_wstream = socket.get_writable_stream();

        connection_results async_results = co_await get_google_results_async(socket_rstream, socket_wstream);

        EXPECT_EQ(get_status_line(async_results.response), get_status_line(sync_results.response)) << "Status lines should be the same";
        EXPECT_EQ(get_body(async_results.response), get_body(sync_results.response))
            << "Bodies should be the same";

        co_await socket.close();
    };

    sync_wait(task_fn());
}

// TODO: Make another test case with server running in one thread and sync client running in another

task<void> handle_server_side_async(tcp_socket &client_peer);
task<void> handle_client_side_async(tcp_socket &client);

TEST_CASE(TestSocketPubSub)
{

    throw std::logic_error("not implemented yet");

    runtime_context context;

    async_event signal;
    const std::string localhost = "127.0.0.1";
    const uint16_t port = 5000;

    auto listener_fn = [&]() -> task<void>
    {
        tcp_listener listener = co_await make_tcp_listener();

        co_await listener.bind({localhost, port});

        co_await listener.listen(1);

        // send the signal that server is ready for connections
        signal.set();

        tcp_socket client_peer = co_await listener.accept();
        co_await handle_server_side_async(client_peer);
    };

    auto socket_fn = [&]() -> task<void>
    {
        tcp_socket client = co_await make_tcp_socket();

        // Wait until server is set up
        co_await signal;

        co_await client.connect({localhost, port});
        EXPECT_EQ(localhost, client.get_remote_host()) << "Remote host should match";
        EXPECT_EQ(port, client.get_remote_port()) << "Remote port should match";

        co_await handle_client_side_async(client);
    };

    sync_wait(when_all(listener_fn(), socket_fn()));
}

#ifdef __linux__
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>

connection_results get_google_results_sync()
{
    connection_results results;

    // --- Resolve hostname ---
    addrinfo hints{}, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;     // IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM; // TCP

    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0)
    {
        throw std::runtime_error("getaddrinfo failed");
    }

    // --- Create socket ---
    int sockfd = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sockfd < 0)
    {
        freeaddrinfo(res);
        throw std::runtime_error("socket failed");
    }

    // --- Connect ---
    if (connect(sockfd, res->ai_addr, res->ai_addrlen) < 0)
    {
        close(sockfd);
        freeaddrinfo(res);
        throw std::runtime_error("connect failed");
    }

    freeaddrinfo(res);

    // --- Send HTTP GET request ---
    std::string request =
        "GET / HTTP/1.1\r\n"
        "Host: google.com\r\n"
        "Connection: close\r\n"
        "\r\n";

    if (send(sockfd, request.c_str(), request.size(), 0) < 0)
    {
        close(sockfd);
        throw std::runtime_error("send failed");
    }

    // --- Read response ---
    char buffer[4096];
    ssize_t bytes_received;
    while ((bytes_received = recv(sockfd, buffer, sizeof(buffer), 0)) > 0)
    {
        results.response.append(buffer, bytes_received);
    }

    if (bytes_received < 0)
    {
        close(sockfd);
        throw std::runtime_error("recv failed");
    }

    close(sockfd);

    return results;
}

#endif

task<connection_results> get_google_results_async(tcp_rstream &rstream, tcp_wstream &wstream)
{
    connection_results results;

    // --- Send HTTP GET request ---
    std::string request =
        "GET / HTTP/1.1\r\n"
        "Host: google.com\r\n"
        "Connection: close\r\n"
        "\r\n";

    co_await wstream.send(std::span<const char>(request.begin(), request.end()));
    co_await wstream.close();

    // --- Read response ---
    std::vector<char> content = co_await (rstream | collect<std::vector<char>, char>(collectors::to_vector<char>()));
    results.response = std::string(content.begin(), content.end());
    co_await rstream.close();

    co_return results;
}

task<void> handle_server_side_async(tcp_socket &client_peer)
{
    throw std::runtime_error("not implemented yet");
}

task<void> handle_client_side_async(tcp_socket &client)
{
    throw std::runtime_error("not implemented yet");
}
