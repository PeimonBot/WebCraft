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
    runtime_context context;
    connection_results results;
    EXPECT_NO_THROW(results = get_google_results_sync()) << "Internet should be available";

    EXPECT_FALSE(results.response.empty()) << "Response should not be empty";
}

task<connection_results> get_google_results_async(tcp_rstream &rstream, tcp_wstream &wstream);

TEST_CASE(TestSocketConnection)
{
    throw std::runtime_error("Test not implemented");
    runtime_context context;

    connection_results sync_results = get_google_results_sync();

    auto task_fn = [&]() -> task<void>
    {
        auto socket = make_tcp_socket();

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

task<void> handle_server_side_async(tcp_socket &client_peer);
task<void> handle_client_side_async(tcp_socket &client);
void handle_client_side_sync(const std::string &host, uint16_t port);

TEST_CASE(TestAsyncServerSyncClient)
{
    throw std::runtime_error("Test not implemented");

    runtime_context context;

    async_event signal;
    const std::string localhost = "127.0.0.1";
    const uint16_t port = 5001;

    auto listener_fn = [&]() -> task<void>
    {
        tcp_listener listener = make_tcp_listener();
        std::cout << "Async Server: Server socket was made." << std::endl;

        listener.bind({localhost, port});
        std::cout << "Async Server: Server socket was bound to " << localhost << ":" << port << std::endl;

        listener.listen(1);
        std::cout << "Async Server: Server socket is now listening on " << localhost << ":" << port << std::endl;

        // send the signal that server is ready for connections
        signal.set();

        tcp_socket client_peer = co_await listener.accept();
        co_await handle_server_side_async(client_peer);
        std::cout << "Async Server: All went well" << std::endl;
    };

    std::thread client_thread([&]()
                              {
        sync_wait(co_async {
            co_await signal;
        }());
        std::cout << "Sync Client: Starting sync client" << std::endl;
        handle_client_side_sync(localhost, port);
        std::cout << "Sync Client: All went well" << std::endl; });

    sync_wait(listener_fn());
    client_thread.join();
    std::cout << "Mixed async/sync test completed successfully" << std::endl;
}

TEST_CASE(TestSocketPubSub)
{
    throw std::runtime_error("Test not implemented");

    runtime_context context;

    async_event signal;
    const std::string localhost = "127.0.0.1";
    const uint16_t port = 5000;

    auto listener_fn = [&]() -> task<void>
    {
        tcp_listener listener = make_tcp_listener();
        std::cout << "Server: Server socket was made." << std::endl;

        std::cout << "Server: Preparing to bind" << std::endl;
        listener.bind({localhost, port});
        std::cout << "Server: Server socket was bound to " << localhost << ":" << port << std::endl;

        listener.listen(1);
        std::cout << "Server: Server socket is now listening on " << localhost << ":" << port << std::endl;

        // send the signal that server is ready for connections
        signal.set();

        tcp_socket client_peer = co_await listener.accept();
        co_await handle_server_side_async(client_peer);
        std::cout << "Server: All went well" << std::endl;
    };

    auto socket_fn = [&]() -> task<void>
    {
        tcp_socket client = make_tcp_socket();
        std::cout << "Client: Client socket was made." << std::endl;

        // Wait until server is set up
        co_await signal;

        co_await client.connect({localhost, port});
        std::cout << "Client: Connecting to " << localhost << ":" << port << std::endl;
        EXPECT_EQ(localhost, client.get_remote_host()) << "Remote host should match";
        EXPECT_EQ(port, client.get_remote_port()) << "Remote port should match";

        co_await handle_client_side_async(client);
        std::cout << "Client: All went well" << std::endl;
    };

    sync_wait(when_all(listener_fn(), socket_fn()));
    std::cout << "Everything went well" << std::endl;
}

const std::string content = "Hello World!";
constexpr size_t content_size = 12;

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <WinSock2.h>
#include <WS2tcpip.h>

#else
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define SOCKET int
#define INVALID_SOCKET (-1)

void closesocket(SOCKET fd)
{
    ::close(fd);
}

#endif

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
        std::cout << "Address failed: " << host.c_str() << ":" << port << std::endl;
        std::cout << std::endl;
        throw std::runtime_error("getaddrinfo failed");
    }

    // --- Create socket ---
    SOCKET sockfd = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sockfd == INVALID_SOCKET)
    {
        freeaddrinfo(res);
        throw std::runtime_error("socket failed");
    }

    // --- Connect ---
    if (connect(sockfd, res->ai_addr, (int)res->ai_addrlen) < 0)
    {
        closesocket(sockfd);
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

    if (send(sockfd, request.c_str(), (int)request.size(), 0) < 0)
    {
        closesocket(sockfd);
        throw std::runtime_error("send failed");
    }

    // --- Read response ---
    char buffer[4096];
    int bytes_received;
    while ((bytes_received = recv(sockfd, buffer, sizeof(buffer), 0)) > 0)
    {
        results.response.append(buffer, bytes_received);
    }

    if (bytes_received < 0)
    {
        closesocket(sockfd);
        throw std::runtime_error("recv failed");
    }

    closesocket(sockfd);

    return results;
}

void handle_client_side_sync(const std::string &host, uint16_t port)
{
    // --- Create socket ---
    SOCKET sockfd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == INVALID_SOCKET)
    {
        throw std::runtime_error("socket failed");
    }

    // --- Connect ---
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr);

    if (connect(sockfd, reinterpret_cast<sockaddr *>(&server_addr), sizeof(server_addr)) < 0)
    {
        closesocket(sockfd);
        throw std::runtime_error("connect failed");
    }

    // --- Send data ---
    std::array<char, 12> wbuffer;
    std::copy(content.begin(), content.end(), wbuffer.begin());

    if (send(sockfd, wbuffer.data(), (int)wbuffer.size(), 0) < 0)
    {
        closesocket(sockfd);
        throw std::runtime_error("send failed");
    }
    std::cout << "Sync Client: Sent data to server" << std::endl;

    // --- Receive data ---
    std::array<char, 12> rbuffer;
    int bytes_received = recv(sockfd, rbuffer.data(), (int)rbuffer.size(), 0);
    if (bytes_received < 0)
    {
        closesocket(sockfd);
        throw std::runtime_error("recv failed");
    }
    std::cout << "Sync Client: Received " << bytes_received << " bytes from server" << std::endl;

    EXPECT_EQ(bytes_received, content.size()) << "Bytes received should be " << content.size();
    EXPECT_EQ(wbuffer, rbuffer) << "Sent and received data should match";

    closesocket(sockfd);
}

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
    auto &client_peer_rstream = client_peer.get_readable_stream();
    auto &client_peer_wstream = client_peer.get_writable_stream();

    std::array<char, content_size> buffer;
    size_t bytes_received = co_await client_peer_rstream.recv(std::span<char>(buffer.begin(), buffer.end()));
    EXPECT_EQ(bytes_received, content_size) << "Bytes received should be " << content_size;
    std::cout << "Server: Received from client: " << bytes_received << std::endl;
    bool sent = co_await client_peer_wstream.send(std::span<const char>(buffer.begin(), buffer.end()));
    EXPECT_TRUE(sent) << "Data should be sent";
    std::cout << "Server: Sent the bytes to client: " << sent << std::endl;
}

task<void> handle_client_side_async(tcp_socket &client)
{
    auto &client_rstream = client.get_readable_stream();
    auto &client_wstream = client.get_writable_stream();

    std::array<char, 12> rbuffer;
    std::array<char, 12> wbuffer;
    std::copy(content.begin(), content.end(), wbuffer.begin());

    bool sent = co_await client_wstream.send(wbuffer);
    EXPECT_TRUE(sent) << "Data should be sent";
    std::cout << "Client: Sent the bytes to server: " << sent << std::endl;
    size_t bytes_received = co_await client_rstream.recv(rbuffer);
    EXPECT_EQ(bytes_received, content.size()) << "Bytes received should be " << content.size();
    std::cout << "Client: Received from server: " << bytes_received << std::endl;
    EXPECT_EQ(wbuffer, rbuffer);
}
