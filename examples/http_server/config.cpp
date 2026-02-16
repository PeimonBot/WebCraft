///////////////////////////////////////////////////////////////////////////////
// Implementation of nginx-like config parser.
///////////////////////////////////////////////////////////////////////////////

#include "config.hpp"
#include <fstream>
#include <stdexcept>

namespace http_server {

namespace {

std::string trim(const std::string& s)
{
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end == std::string::npos ? std::string::npos : end - start + 1);
}

std::string trim_directive_impl(const std::string& line)
{
    std::string s = trim(line);
    if (!s.empty() && s.back() == ';')
        s.pop_back();
    return trim(s);
}

// Parse "host:port" or "host" (default port 80). Throws on invalid.
connection_info parse_server_addr(const std::string& addr)
{
    connection_info info;
    std::string s = trim(addr);
    auto colon = s.rfind(':');
    if (colon != std::string::npos && colon > 0)
    {
        info.host = trim(s.substr(0, colon));
        std::string port_str = trim(s.substr(colon + 1));
        try
        {
            int port = std::stoi(port_str);
            if (port <= 0 || port > 65535) throw std::out_of_range("port");
            info.port = static_cast<uint16_t>(port);
        }
        catch (const std::exception&)
        {
            throw std::invalid_argument("invalid port in server: " + addr);
        }
    }
    else
    {
        info.host = s.empty() ? "127.0.0.1" : s;
        info.port = 80;
    }
    return info;
}

// Simple tokenizer: next token (space/brace separated). Modifies line and returns next token.
std::string next_token(std::string& line)
{
    line = trim(line);
    if (line.empty()) return "";
    size_t i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    if (i >= line.size()) { line.clear(); return ""; }
    size_t start = i;
    if (line[i] == '{' || line[i] == '}' || line[i] == ';')
    {
        line = trim(line.substr(i + 1));
        return line.substr(start, 1);
    }
    while (i < line.size() && line[i] != ' ' && line[i] != '\t' && line[i] != '{' && line[i] != '}' && line[i] != ';')
        ++i;
    std::string tok = line.substr(start, i - start);
    line = trim(line.substr(i));
    return tok;
}

}  // namespace

std::string trim_directive(const std::string& line)
{
    return trim_directive_impl(line);
}

std::optional<config> load_config(const std::string& path)
{
    std::ifstream f(path);
    if (!f) return std::nullopt;

    config cfg;
    std::string line;
    int line_no = 0;

    try
    {
        while (std::getline(f, line))
        {
            ++line_no;
            line = trim(line);
            if (line.empty() || line[0] == '#') continue;

            if (line.find("upstream") == 0)
            {
                std::string rest = trim(line.substr(8));
                std::string name = next_token(rest);
                if (name.empty()) throw std::runtime_error("upstream: missing name at line " + std::to_string(line_no));
                if (next_token(rest) != "{") throw std::runtime_error("upstream: expected { at line " + std::to_string(line_no));

                upstream up;
                up.name = name;
                while (std::getline(f, line))
                {
                    ++line_no;
                    line = trim_directive_impl(line);
                    if (line.empty() || line[0] == '#') continue;
                    if (line == "}") break;
                    if (line.find("server") == 0)
                    {
                        std::string addr = trim(line.substr(6));
                        if (!addr.empty())
                            up.servers.push_back(parse_server_addr(addr));
                    }
                }
                if (up.servers.empty())
                    throw std::runtime_error("upstream " + name + " has no servers");
                cfg.upstreams[name] = std::move(up);
                continue;
            }

            if (line.find("server") == 0)
            {
                std::string rest = trim(line.substr(6));
                if (next_token(rest) != "{") throw std::runtime_error("server: expected { at line " + std::to_string(line_no));

                server_block block;
                while (std::getline(f, line))
                {
                    ++line_no;
                    std::string orig = line;
                    line = trim(line);
                    if (line.empty() || line[0] == '#') continue;
                    line = trim_directive_impl(orig);
                    if (line.empty()) continue;
                    if (line == "}") break;

                    if (line.find("listen") == 0)
                    {
                        std::string port_str = trim(line.substr(6));
                        int port = std::stoi(port_str);
                        if (port <= 0 || port > 65535) throw std::runtime_error("invalid listen port at line " + std::to_string(line_no));
                        block.listen_port = static_cast<uint16_t>(port);
                        continue;
                    }

                    if (line.find("location") == 0)
                    {
                        std::string rest = trim(line.substr(8));
                        std::string path_prefix = next_token(rest);
                        if (path_prefix.empty() || path_prefix[0] != '/')
                            throw std::runtime_error("location: expected path at line " + std::to_string(line_no));
                        if (next_token(rest) != "{")
                            throw std::runtime_error("location: expected { at line " + std::to_string(line_no));

                        location loc;
                        loc.path_prefix = path_prefix;
                        loc.proxy_upstream = "";

                        while (std::getline(f, line))
                        {
                            ++line_no;
                            std::string orig_inner = line;
                            line = trim(line);
                            if (line.empty() || line[0] == '#') continue;
                            line = trim_directive_impl(orig_inner);
                            if (line == "}") break;

                            if (line.find("proxy_pass") == 0)
                            {
                                std::string value = trim(line.substr(10));
                                // expect "http://upstream_name" or "http://upstream_name/"
                                if (value.size() >= 7 && (value.substr(0, 7) == "http://" || value.substr(0, 8) == "https://"))
                                {
                                    size_t start = value.find("//") + 2;
                                    size_t end = value.find_first_of("/ \t");
                                    if (end == std::string::npos) end = value.size();
                                    loc.proxy_upstream = value.substr(start, end - start);
                                }
                            }
                        }
                        block.locations.push_back(std::move(loc));
                        continue;
                    }
                }
                if (block.listen_port != 0)
                    cfg.servers.push_back(std::move(block));
                continue;
            }
        }
    }
    catch (const std::exception&)
    {
        return std::nullopt;
    }

    return cfg;
}

}  // namespace http_server
