#define TEST_SUITE_NAME WindowsSocketTestSuite

#include "test_suite.hpp"
#include <iostream>
#include <string>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")

const std::string host = "google.com";
const uint16_t port = 80;

// Helper to dump buffer content as hex for debugging
namespace
{
    std::string dump_buffer_hex(const char *buffer, size_t size, size_t max_bytes = 64)
    {
        std::string result;
        for (size_t i = 0; i < std::min(size, max_bytes); ++i)
        {
            char hex[4];
            sprintf_s(hex, "%02X ", (unsigned char)buffer[i]);
            result += hex;
            if ((i + 1) % 16 == 0)
                result += "\n";
        }
        return result;
    }
}

HANDLE iocp;
SOCKET sock;

bool initialize()
{
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        return false;
    }

    iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    return iocp != nullptr;
}

void cleanup()
{
    if (sock != INVALID_SOCKET)
    {
        closesocket(sock);
        sock = INVALID_SOCKET;
    }
    if (iocp != nullptr)
    {
        CloseHandle(iocp);
        iocp = nullptr;
    }
    WSACleanup();
}

bool setup_connection()
{
    addrinfo hints{}, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0)
    {
        return false;
    }

    sock = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock == INVALID_SOCKET)
    {
        freeaddrinfo(res);
        return false;
    }

    HANDLE iocpHandle = CreateIoCompletionPort((HANDLE)sock, iocp, (ULONG_PTR)sock, 0);
    if (iocpHandle == nullptr)
    {
        freeaddrinfo(res);
        return false;
    }

    bool connected = connect(sock, res->ai_addr, (int)res->ai_addrlen) >= 0;
    freeaddrinfo(res);
    return connected;
}

std::string test_original_approach()
{
    std::cout << "=== Testing Original Approach (Broken) ===" << std::endl;

    if (!setup_connection())
    {
        throw std::runtime_error("Connection failed");
    }

    std::string request =
        "GET / HTTP/1.1\r\n"
        "Host: google.com\r\n"
        "Connection: close\r\n"
        "\r\n";

    // Send
    OVERLAPPED sendOverlapped = {};
    sendOverlapped.hEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    WSABUF sendBuf;
    sendBuf.len = (ULONG)request.size();
    sendBuf.buf = (char *)request.c_str();
    DWORD bytesSent = 0;

    int result = WSASend(sock, &sendBuf, 1, &bytesSent, 0, &sendOverlapped, nullptr);
    if (result == SOCKET_ERROR && WSAGetLastError() == WSA_IO_PENDING)
    {
        DWORD bytesTransferred;
        ULONG_PTR completionKey;
        LPOVERLAPPED overlapped;
        GetQueuedCompletionStatus(iocp, &bytesTransferred, &completionKey, &overlapped, 5000);
        WaitForSingleObject(sendOverlapped.hEvent, 5000);
        bytesSent = bytesTransferred;
    }

    std::cout << "Original: Sent " << bytesSent << " bytes" << std::endl;

    // Receive - using potentially same memory area
    std::vector<char> response;
    char buffer[4096]; // Stack buffer - potentially problematic

    OVERLAPPED recvOverlapped = {}; // Reusing similar pattern
    recvOverlapped.hEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    WSABUF recvBuf;
    recvBuf.len = sizeof(buffer);
    recvBuf.buf = buffer;
    DWORD bytesReceived = 0;
    DWORD flags = 0;

    result = WSARecv(sock, &recvBuf, 1, &bytesReceived, &flags, &recvOverlapped, nullptr);
    if (result == SOCKET_ERROR && WSAGetLastError() == WSA_IO_PENDING)
    {
        DWORD bytesTransferred;
        ULONG_PTR completionKey;
        LPOVERLAPPED overlapped;
        GetQueuedCompletionStatus(iocp, &bytesTransferred, &completionKey, &overlapped, 5000);
        WaitForSingleObject(recvOverlapped.hEvent, 5000);
        bytesReceived = bytesTransferred;
    }

    if (bytesReceived > 0)
    {
        std::cout << "Original: First receive got " << bytesReceived << " bytes" << std::endl;
        std::cout << "Original: First 64 bytes hex dump:\n"
                  << ::dump_buffer_hex(buffer, bytesReceived) << std::endl;
        response.insert(response.end(), buffer, buffer + bytesReceived);
    }

    return std::string(response.begin(), response.end());
}

TEST_CASE(TestOriginalApproach)
{
    EXPECT_TRUE(initialize()) << "IOCP initialization should succeed";

    std::string response;
    EXPECT_NO_THROW(response = test_original_approach())
        << "Should execute without throwing";

    // This test is expected to show the garbage data issue
    std::cout << "Original approach response length: " << response.size() << std::endl;
    if (!response.empty())
    {
        std::cout << "Original response starts with: " << response.substr(0, std::min(size_t(50), response.size())) << std::endl;
    }

    cleanup();
}