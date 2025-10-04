#include <webcraft/web/core.hpp>

namespace webcraft::web::core
{
    std::expected<uri, std::string> uri::parse(std::string_view uri_str) noexcept
    {
        try
        {
            uri result{std::string{uri_str}};
            std::string_view remaining = result.uri_string;

            // Parse fragment first (rightmost component)
            if (auto fragment_pos = remaining.rfind('#'); fragment_pos != std::string_view::npos)
            {
                result.fragment_part = remaining.substr(fragment_pos + 1);
                result.has_fragment = true;
                remaining = remaining.substr(0, fragment_pos);
            }

            // Parse scheme
            if (auto colon_pos = remaining.find(':'); colon_pos != std::string_view::npos)
            {
                // Check if this is actually a scheme (not part of authority like port)
                bool is_scheme = true;
                for (size_t i = 0; i < colon_pos; ++i)
                {
                    char c = remaining[i];
                    if (i == 0)
                    {
                        if (!std::isalpha(c))
                        {
                            is_scheme = false;
                            break;
                        }
                    }
                    else
                    {
                        if (!(std::isalnum(c) || c == '+' || c == '-' || c == '.'))
                        {
                            is_scheme = false;
                            break;
                        }
                    }
                }

                if (is_scheme)
                {
                    result.scheme_part = remaining.substr(0, colon_pos);
                    result.has_scheme = true;
                    remaining = remaining.substr(colon_pos + 1);
                }
            }

            result.scheme_specific_part_view = remaining;

            // If hierarchical, parse authority, path, and query
            if (result.is_hierarchical())
            {
                // Parse authority
                if (remaining.starts_with("//"))
                {
                    remaining = remaining.substr(2);
                    auto authority_end = remaining.find_first_of("/?");
                    if (authority_end == std::string_view::npos)
                    {
                        authority_end = remaining.size();
                    }

                    result.authority_part = remaining.substr(0, authority_end);
                    result.has_authority = true;
                    remaining = remaining.substr(authority_end);

                    // Parse authority components
                    std::string_view auth = result.authority_part;

                    // Parse userinfo
                    if (auto at_pos = auth.find('@'); at_pos != std::string_view::npos)
                    {
                        result.userinfo_part = auth.substr(0, at_pos);
                        result.has_userinfo = true;
                        auth = auth.substr(at_pos + 1);
                    }

                    // Parse host and port
                    if (!auth.empty())
                    {
                        if (auth.front() == '[')
                        {
                            // IPv6 address
                            auto bracket_end = auth.find(']');
                            if (bracket_end != std::string_view::npos)
                            {
                                result.host_part = auth.substr(0, bracket_end + 1);
                                result.has_host = true;
                                auth = auth.substr(bracket_end + 1);

                                if (!auth.empty() && auth.front() == ':')
                                {
                                    auth = auth.substr(1);
                                    if (!auth.empty())
                                    {
                                        auto port_str = std::string{auth};
                                        char *end;
                                        auto port_val = std::strtoul(port_str.c_str(), &end, 10);
                                        if (*end == '\0' && port_val <= 65535)
                                        {
                                            result.port_number = static_cast<uint16_t>(port_val);
                                            result.has_port = true;
                                        }
                                    }
                                }
                            }
                        }
                        else
                        {
                            // IPv4 or hostname
                            auto colon_pos = auth.rfind(':');
                            if (colon_pos != std::string_view::npos)
                            {
                                // Check if this is a port
                                auto port_part = auth.substr(colon_pos + 1);
                                bool is_port = !port_part.empty();
                                for (char c : port_part)
                                {
                                    if (!std::isdigit(c))
                                    {
                                        is_port = false;
                                        break;
                                    }
                                }

                                if (is_port)
                                {
                                    result.host_part = auth.substr(0, colon_pos);
                                    result.has_host = true;

                                    auto port_str = std::string{port_part};
                                    char *end;
                                    auto port_val = std::strtoul(port_str.c_str(), &end, 10);
                                    if (*end == '\0' && port_val <= 65535)
                                    {
                                        result.port_number = static_cast<uint16_t>(port_val);
                                        result.has_port = true;
                                    }
                                }
                                else
                                {
                                    result.host_part = auth;
                                    result.has_host = true;
                                }
                            }
                            else
                            {
                                result.host_part = auth;
                                result.has_host = true;
                            }
                        }
                    }
                }

                // Parse query
                if (auto query_pos = remaining.find('?'); query_pos != std::string_view::npos)
                {
                    result.query_part = remaining.substr(query_pos + 1);
                    result.has_query = true;
                    remaining = remaining.substr(0, query_pos);
                }

                // Remaining is path
                result.path_part = remaining;
            }

            return result;
        }
        catch (const std::exception &e)
        {
            return std::unexpected(std::string("URI parsing error: ") + e.what());
        }
        catch (...)
        {
            return std::unexpected("Unknown URI parsing error");
        }
    }

    // URI Builder implementation
    uri_builder::uri_builder(const uri &u)
    {
        if (auto s = u.scheme())
        {
            scheme_str = *s;
            has_scheme_val = true;
        }

        if (auto ui = u.userinfo())
        {
            userinfo_str = *ui;
            has_userinfo_val = true;
        }

        if (auto h = u.host())
        {
            host_str = *h;
            has_host_val = true;
        }

        if (auto p = u.port())
        {
            port_num = *p;
            has_port_val = true;
        }

        if (auto path = u.path())
        {
            path_str = *path;
        }

        if (auto q = u.query())
        {
            query_str = *q;
            has_query_val = true;
        }

        if (auto f = u.fragment())
        {
            fragment_str = *f;
            has_fragment_val = true;
        }
    }

    uri_builder &uri_builder::scheme(std::string_view scheme)
    {
        scheme_str = scheme;
        has_scheme_val = true;
        return *this;
    }

    uri_builder &uri_builder::userinfo(std::string_view userinfo)
    {
        userinfo_str = userinfo;
        has_userinfo_val = true;
        return *this;
    }

    uri_builder &uri_builder::host(std::string_view host)
    {
        host_str = host;
        has_host_val = true;
        return *this;
    }

    uri_builder &uri_builder::port(uint16_t port)
    {
        port_num = port;
        has_port_val = true;
        return *this;
    }

    uri_builder &uri_builder::path(std::string_view path)
    {
        path_str = path;
        return *this;
    }

    uri_builder &uri_builder::append_path(std::string_view segment)
    {
        if (!path_str.empty() && path_str.back() != '/')
        {
            path_str += '/';
        }
        else if (path_str.empty())
        {
            path_str = '/';
        }
        path_str += segment;
        return *this;
    }

    uri_builder &uri_builder::query(std::string_view query)
    {
        query_str = query;
        has_query_val = true;
        return *this;
    }

    uri_builder &uri_builder::append_query_param(std::string_view name, std::string_view value)
    {
        if (!query_str.empty())
        {
            query_str += '&';
        }
        query_str += name;
        query_str += '=';
        query_str += value;
        has_query_val = true;
        return *this;
    }

    uri_builder &uri_builder::fragment(std::string_view fragment)
    {
        fragment_str = fragment;
        has_fragment_val = true;
        return *this;
    }

    uri_builder &uri_builder::authority(std::string_view userinfo, std::string_view host, std::optional<uint16_t> port)
    {
        if (!userinfo.empty())
        {
            this->userinfo(userinfo);
        }
        this->host(host);
        if (port.has_value())
        {
            this->port(*port);
        }
        return *this;
    }

    uri_builder &uri_builder::authority(std::string_view host, std::optional<uint16_t> port)
    {
        this->host(host);
        if (port.has_value())
        {
            this->port(*port);
        }
        return *this;
    }

    std::expected<uri, std::string> uri_builder::build() const
    {
        return uri::parse(build_string());
    }

    std::string uri_builder::build_string() const
    {
        std::string uri_string;

        // Add scheme if present
        if (has_scheme_val)
        {
            uri_string += scheme_str;
            uri_string += ':';
        }

        // Add authority if we have host
        if (has_host_val)
        {
            uri_string += "//";

            // Add userinfo if present
            if (has_userinfo_val && !userinfo_str.empty())
            {
                uri_string += userinfo_str;
                uri_string += '@';
            }

            // Add host
            uri_string += host_str;

            // Add port if present
            if (has_port_val && port_num.has_value())
            {
                uri_string += ':';
                uri_string += std::to_string(*port_num);
            }
        }

        // Add path
        uri_string += path_str;

        // Add query if present
        if (has_query_val && !query_str.empty())
        {
            uri_string += '?';
            uri_string += query_str;
        }

        // Add fragment if present
        if (has_fragment_val && !fragment_str.empty())
        {
            uri_string += '#';
            uri_string += fragment_str;
        }

        return uri_string;
    }

}