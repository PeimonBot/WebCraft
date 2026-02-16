///////////////////////////////////////////////////////////////////////////////
// Simple nginx-like configuration parser for WebCraft HTTP server.
// Supports: upstream { server host:port; } and server { listen N; location /path { proxy_pass http://upstream; } }
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include <webcraft/async/io/socket.hpp>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace http_server {

using connection_info = webcraft::async::io::socket::connection_info;

/// One or more backend servers (host:port) under a single name (round-robin).
struct upstream {
    std::string name;
    std::vector<connection_info> servers;
};

/// A route: path prefix and optional proxy upstream name (empty = local response).
struct location {
    std::string path_prefix;
    std::string proxy_upstream;  // empty if not proxy_pass
};

/// Server block: listen port and ordered list of location rules.
struct server_block {
    uint16_t listen_port{0};
    std::vector<location> locations;
};

/// Full config: named upstreams and server blocks.
struct config {
    std::unordered_map<std::string, upstream> upstreams;
    std::vector<server_block> servers;
};

/// Load config from a file path. Returns nullopt on parse or file error.
std::optional<config> load_config(const std::string& path);

/// Trim leading/trailing whitespace and strip trailing ';'.
std::string trim_directive(const std::string& line);

}  // namespace http_server
