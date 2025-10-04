#pragma once

#include "core.hpp"

namespace webcraft::web::client
{

    class web_client;
    class web_connection_builder;

    class web_response_base;
    class web_client_connection;
    class web_socket_connection;
    class web_response;

    class web_response_base
    {
    public:
        std::optional<std::string> get_response_header(const std::string &name) const;
        std::vector<std::string> get_response_headers(const std::string &name) const;
        const std::unordered_map<std::string, std::vector<std::string>> &get_response_headers() const;

        const webcraft::web::core::response_code get_response_code() const;
        const webcraft::web::core::http_method get_request_method() const;
        const webcraft::web::core::uri &get_request_uri() const;
        const webcraft::web::connection::connection_protocol get_connection_protocol() const;
    };

    class web_client_connection : public web_response_base
    {
    public:
        virtual ~web_client_connection();

        async_t(void) close();
        webcraft::web::core::web_read_stream auto &get_read_stream();
        webcraft::web::core::web_write_stream auto &get_write_stream();
    };

    class web_socket_connection : public web_response_base
    {
    public:
        virtual ~web_socket_connection();

        async_t(void) close();
        webcraft::web::core::web_read_stream auto &get_read_stream();
        webcraft::web::core::web_write_stream auto &get_write_stream();
    };

    class web_response : public web_response_base
    {
    public:
        virtual ~web_response();

        template <typename T>
        async_t(T) get_payload(webcraft::web::core::payload_handler<T> auto &&handler);
    };

    class web_client
    {
    public:
        web_client();
        virtual ~web_client();

        async_t(void) close();
        web_connection_builder connect();
    };

    class web_connection_builder
    {
    public:
        friend class web_client;

        web_connection_builder(web_client &client);
        virtual ~web_connection_builder();

        web_connection_builder &path(const uri &u);
        web_connection_builder &method(webcraft::web::core::http_method method);
        web_connection_builder &headers(const std::unordered_map<std::string, std::vector<std::string>> &headers);
        web_connection_builder &header(const std::string &key, const std::string &value);
        web_connection_builder &timeout(std::chrono::milliseconds timeout);
        web_connection_builder &follow_redirects(bool follow);
        web_connection_builder &max_redirects(size_t max_redirects);
        web_connection_builder &proxy(const uri &proxy_uri);

        async_t(web_client_connection) request_raw();
        async_t(web_socket_connection) request_websocket();
        async_t(web_response) send_request(webcraft::web::core::payload_dispatcher auto &&dispatcher);
    };
}