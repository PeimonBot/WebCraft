///////////////////////////////////////////////////////////////////////////////
// HTTP server example with reverse proxy (proxy_pass) and round-robin load balancing.
// Uses a simple nginx-like config file to define backends and routes.
///////////////////////////////////////////////////////////////////////////////

#include "config.hpp"
#include <webcraft/async/async.hpp>
#include <webcraft/async/io/io.hpp>
#include <atomic>
#include <iostream>
#include <string>
#include <string_view>

using namespace webcraft::async;
using namespace webcraft::async::io::socket;

namespace {

constexpr std::string_view LOCAL_RESPONSE =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/html; charset=utf-8\r\n"
    "Connection: close\r\n"
    "\r\n"
    "<!DOCTYPE html><html><head><title>WebCraft</title></head>"
    "<body><h1>Hello from WebCraft HTTP server</h1></body></html>";

// Round-robin state per upstream name.
struct proxy_state {
    config cfg;
    std::unordered_map<std::string, std::atomic<std::size_t>> next_index;
};

proxy_state* g_proxy_state = nullptr;

const location* find_location(const server_block& block, std::string_view path)
{
    const location* best = nullptr;
    std::size_t best_len = 0;
    for (const auto& loc : block.locations)
    {
        if (path.size() >= loc.path_prefix.size() &&
            path.substr(0, loc.path_prefix.size()) == loc.path_prefix &&
            loc.path_prefix.size() > best_len)
        {
            best = &loc;
            best_len = loc.path_prefix.size();
        }
    }
    return best;
}

// Forward request from client to backend and response from backend to client.
fire_and_forget_task proxy_request(tcp_socket& client_socket, std::string_view request_head_and_body,
                                   const connection_info& backend)
{
    auto& client_reader = client_socket.get_readable_stream();
    auto& client_writer = client_socket.get_writable_stream();
    std::vector<char> buffer(8192);

    tcp_socket backend_socket(make_tcp_socket());
    co_await backend_socket.connect(backend);
    auto& backend_reader = backend_socket.get_readable_stream();
    auto& backend_writer = backend_socket.get_writable_stream();

    // Send client request to backend (we already read the full request head/body)
    (void)co_await backend_writer.send(std::span<const char>(request_head_and_body.data(), request_head_and_body.size()));

    // Stream backend response back to client (until backend closes)
    while (true)
    {
        auto n = co_await backend_reader.recv(std::span<char>(buffer));
        if (n == 0) break;
        auto written = co_await client_writer.send(std::span<const char>(buffer.data(), n));
        if (written == 0) break;
    }

    co_await backend_socket.close();
    co_await client_socket.close();
}

fire_and_forget_task handle_client(tcp_socket client_socket, const server_block* block)
{
    auto& reader = client_socket.get_readable_stream();
    auto& writer = client_socket.get_writable_stream();
    std::vector<char> buffer(8192);

    std::size_t total = 0;
    while (total < buffer.size())
    {
        auto n = co_await reader.recv(std::span<char>(buffer.data() + total, buffer.size() - total));
        if (n == 0) break;
        total += n;
        std::string_view received(buffer.data(), total);
        if (received.find("\r\n\r\n") != std::string_view::npos)
            break;
    }

    if (total == 0)
    {
        co_await client_socket.close();
        co_return;
    }

    std::string_view request(buffer.data(), total);
    // Parse request line: "METHOD /path HTTP/1.x"
    std::size_t first_space = request.find(' ');
    if (first_space == std::string_view::npos)
    {
        (void)co_await writer.send(std::span<const char>(LOCAL_RESPONSE.data(), LOCAL_RESPONSE.size()));
        co_await client_socket.close();
        co_return;
    }
    std::size_t second_space = request.find(' ', first_space + 1);
    if (second_space == std::string_view::npos)
    {
        (void)co_await writer.send(std::span<const char>(LOCAL_RESPONSE.data(), LOCAL_RESPONSE.size()));
        co_await client_socket.close();
        co_return;
    }
    std::string_view path = request.substr(first_space + 1, second_space - first_space - 1);

    const location* loc = nullptr;
    if (block)
        loc = find_location(*block, path);

    if (loc && !loc->proxy_upstream.empty() && g_proxy_state)
    {
        auto it = g_proxy_state->cfg.upstreams.find(loc->proxy_upstream);
        if (it != g_proxy_state->cfg.upstreams.end() && !it->second.servers.empty())
        {
            auto& up = it->second;
            std::size_t idx = g_proxy_state->next_index[loc->proxy_upstream].fetch_add(1) % up.servers.size();
            const connection_info& backend = up.servers[idx];
            proxy_request(client_socket, request, backend);
            co_return;
        }
    }

    // Local response
    (void)co_await writer.send(std::span<const char>(LOCAL_RESPONSE.data(), LOCAL_RESPONSE.size()));
    co_await client_socket.close();
}

task<void> run_server(connection_info listen_info, const server_block* block)
{
    auto listener = make_tcp_listener();
    listener.bind(listen_info);
    listener.listen(128);

    std::cout << "HTTP server listening on " << listen_info.host << ":" << listen_info.port;
    if (block)
        std::cout << " (config: " << block->locations.size() << " location(s))";
    std::cout << std::endl;

    while (true)
    {
        tcp_socket client = co_await listener.accept();
        handle_client(std::move(client), block);
    }
}

}  // namespace

int main(int argc, char* argv[])
{
    std::string config_path = "server.conf";
    if (argc >= 2)
        config_path = argv[1];

    config cfg;
    auto loaded = http_server::load_config(config_path);
    if (loaded)
    {
        cfg = std::move(*loaded);
        proxy_state state;
        state.cfg = cfg;
        for (const auto& [name, up] : cfg.upstreams)
            state.next_index[name].store(0);
        g_proxy_state = &state;

        runtime_context context;
        const server_block* block = cfg.servers.empty() ? nullptr : &cfg.servers[0];
        uint16_t port = block ? block->listen_port : 8080;
        connection_info info{"0.0.0.0", port};
        sync_wait(run_server(info, block));
    }
    else
    {
        std::cout << "Config file '" << config_path << "' not found or invalid; using default (listen 8080, local response only)." << std::endl;
        runtime_context context;
        connection_info info{"0.0.0.0", 8080};
        sync_wait(run_server(info, nullptr));
    }
    return 0;
}
