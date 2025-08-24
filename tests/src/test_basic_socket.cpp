#define TEST_SUITE_NAME BasicSocketTestSuite

#include "test_suite.hpp"
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <WinSock2.h>
#include <WS2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#define SOCKET int
#define INVALID_SOCKET (-1)
#define closesocket close
#endif

const std::string host = "google.com";
const uint16_t port = 80;

class BasicSocketTest
{
public:
    static bool initialize()
    {
#ifdef _WIN32
        WSADATA wsaData;
        return WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
#else
        return true;
#endif
    }

    static void cleanup()
    {
#ifdef _WIN32
        WSACleanup();
#endif
    }

    static std::string test_basic_socket_communication()
    {
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
        SOCKET sockfd = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (sockfd == INVALID_SOCKET)
        {
            freeaddrinfo(res);
            throw std::runtime_error("socket creation failed");
        }

        // --- Connect ---
        if (connect(sockfd, res->ai_addr, (int)res->ai_addrlen) < 0)
        {
            closesocket(sockfd);
            freeaddrinfo(res);
            throw std::runtime_error("connect failed");
        }

        freeaddrinfo(res);
        std::cout << "Basic Socket: Connected to " << host << ":" << port << std::endl;

        // --- Send HTTP GET request ---
        std::string request =
            "GET / HTTP/1.1\r\n"
            "Host: google.com\r\n"
            "Connection: close\r\n"
            "\r\n";

        int sent = send(sockfd, request.c_str(), (int)request.size(), 0);
        if (sent < 0)
        {
            closesocket(sockfd);
            throw std::runtime_error("send failed");
        }

        std::cout << "Basic Socket: Sent " << sent << " bytes" << std::endl;

        // --- Read response ---
        std::vector<char> response;
        char buffer[4096];
        int bytes_received;

        while ((bytes_received = recv(sockfd, buffer, sizeof(buffer), 0)) > 0)
        {
            std::cout << "Basic Socket: Received " << bytes_received << " bytes" << std::endl;
            response.insert(response.end(), buffer, buffer + bytes_received);
        }

        if (bytes_received < 0)
        {
            closesocket(sockfd);
            throw std::runtime_error("recv failed");
        }

        closesocket(sockfd);

        std::cout << "Basic Socket: Total response size: " << response.size() << " bytes" << std::endl;
        return std::string(response.begin(), response.end());
    }
};

TEST_CASE(TestBasicSocketCommunication)
{
    EXPECT_TRUE(BasicSocketTest::initialize()) << "Socket initialization should succeed";

    std::string response;
    EXPECT_NO_THROW(response = BasicSocketTest::test_basic_socket_communication())
        << "Basic socket communication should work";

    EXPECT_FALSE(response.empty()) << "Response should not be empty";
    EXPECT_TRUE(response.find("HTTP/1.1") != std::string::npos)
        << "Response should contain HTTP status line";
    EXPECT_TRUE(response.find("301 Moved") != std::string::npos)
        << "Response should contain expected redirect";

    std::cout << "First 200 chars of response: "
              << response.substr(0, std::min(size_t(200), response.size())) << std::endl;

    BasicSocketTest::cleanup();
}

#ifdef _WIN32
// Test the same thing but with WSASend/WSARecv to isolate the Windows async API issues
TEST_CASE(TestWindowsAsyncAPIs)
{
    EXPECT_TRUE(BasicSocketTest::initialize()) << "Socket initialization should succeed";

    // --- Resolve hostname ---
    addrinfo hints{}, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    EXPECT_EQ(getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res), 0);

    // --- Create socket ---
    SOCKET sockfd = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    EXPECT_NE(sockfd, INVALID_SOCKET);

    // --- Connect ---
    EXPECT_GE(connect(sockfd, res->ai_addr, (int)res->ai_addrlen), 0);
    freeaddrinfo(res);

    std::cout << "WSA Test: Connected to " << host << ":" << port << std::endl;

    // --- Test WSASend (synchronous) ---
    std::string request =
        "GET / HTTP/1.1\r\n"
        "Host: google.com\r\n"
        "Connection: close\r\n"
        "\r\n";

    WSABUF sendBuf;
    sendBuf.len = (ULONG)request.size();
    sendBuf.buf = (char *)request.c_str();
    DWORD bytesSent = 0;

    int result = WSASend(sockfd, &sendBuf, 1, &bytesSent, 0, nullptr, nullptr);
    EXPECT_EQ(result, 0) << "WSASend should succeed";
    EXPECT_EQ(bytesSent, request.size()) << "All bytes should be sent";

    std::cout << "WSA Test: Sent " << bytesSent << " bytes using WSASend" << std::endl;

    // --- Test WSARecv (synchronous) ---
    std::vector<char> response;
    char buffer[4096];
    WSABUF recvBuf;
    recvBuf.len = sizeof(buffer);
    recvBuf.buf = buffer;
    DWORD bytesReceived = 0;
    DWORD flags = 0;

    while (true)
    {
        result = WSARecv(sockfd, &recvBuf, 1, &bytesReceived, &flags, nullptr, nullptr);
        if (result == SOCKET_ERROR)
        {
            int error = WSAGetLastError();
            std::cout << "WSARecv failed with error: " << error << std::endl;
            break;
        }

        if (bytesReceived == 0)
        {
            std::cout << "WSA Test: Connection closed by server" << std::endl;
            break;
        }

        std::cout << "WSA Test: Received " << bytesReceived << " bytes using WSARecv" << std::endl;
        response.insert(response.end(), buffer, buffer + bytesReceived);
    }

    closesocket(sockfd);

    std::cout << "WSA Test: Total response size: " << response.size() << " bytes" << std::endl;

    EXPECT_FALSE(response.empty()) << "Response should not be empty";
    std::string responseStr(response.begin(), response.end());
    EXPECT_TRUE(responseStr.find("HTTP/1.1") != std::string::npos)
        << "Response should contain HTTP status line";

    std::cout << "First 200 chars of WSA response: "
              << responseStr.substr(0, std::min(size_t(200), responseStr.size())) << std::endl;

    BasicSocketTest::cleanup();
}
#endif
