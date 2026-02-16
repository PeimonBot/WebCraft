///////////////////////////////////////////////////////////////////////////////
// Minimal HTTP server example using WebCraft async TCP APIs.
// Listens on 0.0.0.0:8080 and responds to GET with a simple HTML body.
///////////////////////////////////////////////////////////////////////////////

#include <webcraft/async/async.hpp>
#include <webcraft/async/io/io.hpp>
#include <iostream>
#include <string>
#include <string_view>

using namespace webcraft::async;
using namespace webcraft::async::io::socket;

namespace {

constexpr std::string_view HTTP_RESPONSE =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/html; charset=utf-8\r\n"
    "Connection: close\r\n"
    "\r\n"
    "<!DOCTYPE html><html><head><title>WebCraft</title></head>"
    "<body><h1>Hello from WebCraft HTTP server</h1></body></html>";

fire_and_forget_task handle_client(tcp_socket client_socket)
{
    auto& reader = client_socket.get_readable_stream();
    auto& writer = client_socket.get_writable_stream();
    std::vector<char> buffer(4096);

    // Read and discard the request (minimal parsing: just consume until we have a full request line/headers)
    std::size_t total = 0;
    while (total < buffer.size())
    {
        auto n = co_await reader.recv(std::span<char>(buffer.data() + total, buffer.size() - total));
        if (n == 0)
            break;
        total += n;
        std::string_view received(buffer.data(), total);
        if (received.find("\r\n\r\n") != std::string_view::npos)
            break;
    }

    // Send response
    (void)co_await writer.send(std::span<const char>(HTTP_RESPONSE.data(), HTTP_RESPONSE.size()));
    co_await client_socket.close();
}

task<void> run_server(connection_info info)
{
    auto listener = make_tcp_listener();
    listener.bind(info);
    listener.listen(5);

    std::cout << "HTTP server listening on " << info.host << ":" << info.port << std::endl;

    while (true)
    {
        tcp_socket client = co_await listener.accept();
        handle_client(std::move(client));
    }
}

} // namespace

int main()
{
    runtime_context context;
    connection_info info{"0.0.0.0", 8080};
    sync_wait(run_server(info));
    return 0;
}
