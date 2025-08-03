#include <webcraft/async/io/socket.hpp>

using webcraft::async::io::socket::connection_info;
using namespace webcraft::async;

struct mock_tcp_client_descriptor : public webcraft::async::io::socket::tcp_client_descriptor
{
    mock_tcp_client_descriptor(const webcraft::async::io::socket::connection_info &info)
    {
        set_connection_options(info);
        std::cout << "Mock TCP client descriptor created for host: " << info.host << ", port: " << info.port << std::endl;
    }

    ~mock_tcp_client_descriptor() override
    {
        close();
        std::cout << "Mock TCP client descriptor destroyed." << std::endl;
    }

    task<size_t> read_bytes(std::span<char> buffer) override
    {
        std::cout << "Mock read_bytes called with buffer size: " << buffer.size() << std::endl;
        co_return 0; // Simulate no bytes read
    }

    task<size_t> write_bytes(std::span<const char> buffer) override
    {
        std::cout << "Mock write_bytes called with buffer size: " << buffer.size() << std::endl;
        // Mock implementation for testing purposes
        co_return buffer.size(); // Simulate writing all bytes
    }

    task<bool> connect() override
    {
        std::cout << "Mock connect called." << std::endl;
        co_return true; // Simulate successful connection
    }

    void close() override
    {
        std::cout << "Mock close called." << std::endl;
        // Simulate closing the connection
    }
};

struct mock_tcp_server_descriptor : public webcraft::async::io::socket::tcp_server_descriptor
{
    mock_tcp_server_descriptor(const webcraft::async::io::socket::connection_info &info)
    {
        set_connection_options(info);
    }

    ~mock_tcp_server_descriptor() override
    {
        close();
        std::cout << "Mock TCP server descriptor destroyed." << std::endl;
    }

    task<std::shared_ptr<webcraft::async::io::socket::tcp_client_descriptor>> accept() override
    {
        std::cout << "Mock accept called." << std::endl;
        // Simulate accepting a client connection
        co_return std::make_shared<mock_tcp_client_descriptor>(connection_info{"localhost", 8080});
    }

    void listen(size_t backlog) override
    {
        std::cout << "Mock listen called with backlog: " << backlog << std::endl;
        // Simulate listening on the server socket
    }

    void close() override
    {
        std::cout << "Mock server descriptor closed." << std::endl;
        // Simulate closing the server socket
    }
};

std::shared_ptr<webcraft::async::io::socket::tcp_client_descriptor> webcraft::async::io::socket::make_tcp_client_descriptor(const connection_info &info)
{
    // Here you would normally create a real TCP client descriptor.
    // For this example, we return a mock descriptor.
    return std::make_shared<mock_tcp_client_descriptor>(info);
}

std::shared_ptr<webcraft::async::io::socket::tcp_server_descriptor> webcraft::async::io::socket::make_tcp_server_descriptor(const connection_info &info)
{
    // Here you would normally create a real TCP server descriptor.
    // For this example, we return a mock descriptor.
    return std::make_shared<mock_tcp_server_descriptor>(info);
}