# Web Specification

## Introduction

The backbone of web modules defined here are based off of the async runtime and async io modules.

**NOTE: You'll notice in this documentation, I'm mentioning the usage of HTTP/3 and UDP protocol even though my current codebase does not have a specification or support for UDP. That would be a newer addition to the framework, an issue for it will be made and either I or (an) open source developer(s) will be working on getting that done with async support.**

## Implementation

## namespace webcraft::web::core

### message

```cpp
using message = std::span<const char>;
```

### web_read_stream

```cpp
template<typename T>
concept web_read_stream = async_buffered_readable_stream<T, char> && async_closable_stream<T>;
```

### web_write_stream

```cpp
template<typename T>
concept web_write_stream = async_buffered_writable_stream<T, char> && async_closable_stream<T>;
```

### http_method

```cpp
enum class http_method
{
    GET,
    POST,
    PUT,
    DELETE,
    OPTIONS,
    ...
};
```

### http_response_code

```cpp
enum class http_code
{
    // 

    // success codes
    SUCCESS=200,
    NO_CONTENT=201
    ...
    // redirect code
    

    //
};
```

### payload_dispatcher

```cpp
template<typename T>
concept payload_dispatcher = requires(T t1, T& t2, T&& t3, web_write_stream& stream) {
    { t1(stream) } -> std::same_as<task<void>>;
    { t2(stream) } -> std::same_as<task<void>>;
    { t3(stream) } -> std::same_as<task<void>>;
};
```

### payload_handler

```cpp
template<typename T, typename Ret>
concept payload_handler = requires(T t1, T& t2, T&& t3, web_read_stream& stream) {
    { t1(stream) } -> std::same_as<task<Ret>>;
    { t2(stream) } -> std::same_as<task<Ret>>;
    { t3(stream) } -> std::same_as<task<Ret>>;
};
```

### headers namespace

There will also be a namespace just for the common headers and their values.

### payloads namespace

There will be a namespace called payloads which has a bunch of functions which help convert to and from C++ friendly objects into `payload_dispatcher` and `payload_handler`.


## namespace webcraft::web::connection

These are the foundational interfaces required to have connections on both the client and server side to be served on TLS or plain text, on TCP or UDP, and on HTTP/1.1, HTTP/2 or HTTP/3.

### connection

The underlying connection for an HTTP connection is defined by the following primitive:

```cpp
class connection
{
public:
    virtual async_t<size_t> send_data(message data) = 0;
    virtual async_t<size_t> recv_data(message data) = 0;
    virtual connection_protocol get_protocol() = 0;
    virtual std::optional<ssl_context> get_ssl_context() { return std::nullopt; };
    virtual std::string get_remote_host();
    virtual uint16_t get_remote_port();
};
```

This underlying connection tells us a couple details about itself: what protocol is it running on, information about the peers host and port, and whether its using tls or not. Apart from that, it also allows us to send and receive data to the peer.

### connection_provider

Not all connections are created the same way and not all connections have the same properties but we do need a centralized mechanism to provide us with such connections:
```cpp
class connection_provider
{
    virtual task<std::shared_ptr<connection>> get_connection() = 0;
    virtual std::vector<connection_protocol> get_supported_protocols() = 0;
    virtual bool tls_supported() = 0; // will return false until tls is implemented
};
```
The implementation of this class would take many circumstances into account: Are we the server accepting connections or are we the client creating these connections? are we using TLS or plain text? are we using HTTP/1.1 or HTTP/2 (both TCP based) or HTTP/3 (UDP based)? The connection provider will give us these details and will allow us to acquire a connection, strategy will be depending on the implementation of this interface. 

#### Usage

For a **client** implementing a `connection_provider`, this would be a specific provider for a particular host and port (localhost:9080, google.com, microsoft.com, ibm.com, etc). Here either new connections are pooled (keep-alive), multiplexed (HTTP/2 or HTTP/3), or spawned (new TCP connection) depending on what protocols the server at that address supports. Similarly `get_supported_protocols` and `tls_supported` are based on what the server supports.

For a **server** implementing a `connection_provider`, this provider would represent the provider of the server. `get_connection` receives a connection, whether its a multiplexed connection, pooled connection, new connection, or a QUIC connection. `get_supported_protocols` is based on what the "server" supports (and is also limited by framework features - can't do HTTP/3 if UDP support is not existant). `tls_supported` is based on whether the server using this provider has enabled TLS.

### connection_protocol

The definition of connection protocol is shown here. It lists out the types of HTTP protocols which will be used.
```cpp
enum class connection_protocol
{
    HTTP_1_0,
    HTTP_1_1,
    HTTP_2,
    HTTP_3
};
```

## namespace webcraft::web::client

The web client API that we provide is inspired by multiple other languages implementation including JS implementations (`fetch`, `axios`, `XMLHTTPRequest`, `WebSocket`, `EventObject`), Java (`HTTPClient`, `WebClient`, `HTTPUrlConnection`), and C# (`WebClient`, `HTTPClient`). This API will implement the connection provider to supply connections to the client. As such, there should be ideally one `web_client` per application since it will pool and multiplex connections.

### web_client

The definition is below. Seems pretty lean right? `web_client` provides the mechanism to create a connection using `web_connection_builder` and its possible to attach or create a `web_connection_builder` via constructor passing in a web_client. Just following fluent pattern:

```cpp
class web_client
{
public:
    void set_ssl_trust_store(ssl_trust_store store); // sets trust store - will throw tls_not_implemented_error until tls is implemented
    ssl_trust_store& get_trust_store() // gets the trust store - will throw tls_not_implemented_error until tls is implemented

    task<void> close(); // closes any outstanding connections that are kept alive

    web_connection_builder connect();
};
```

### web_connection_builder

The definition is below.

```cpp
class web_connection_builder
{
public:
    friend class web_client;

    web_connection_builder(web_client& ref);

    // builders
    web_connection_builder& path(uri target_uri);
    web_connection_builder& method(http_method method); // changes to this have no effect on websocket based connections
    web_connection_builder& headers(std::unordered_map<std::string, std::vector<std::string>> headers);
    web_connection_builder& header(std::string name, std::string value);
    web_connection_builder& proxy(uri proxy_uri);
    web_connection_builder& allow_redirects(bool allow = true);

    task<web_client_connection> request_raw();
    task<web_socket_connection> websocket(); // don't fill in `method` since websockets on HTTP/1.1 and HTTP/2 & 3 are different
    task<web_response> send(payload_dispatcher auto&& sender);
};
```

Through this class, we can build composable builders to spawn templated connections. We can specify the path, the method, the headers, the proxy, redirect handling, and continue building until we are ready to make the connection. We have 3 options at this point. 
1. We can directly handle the connection afterwards and get ourselves a `web_client_connection`.
2. We can create a `web_socket_connection`.
3. We can send a payload by providing a `payload_dispatcher` and receiving a `web_response`.

Through Option 1, we can handle the read and write streams how we want and gain more control over how the connection is actually going to be handled. This is great if you want to do some ping ponging data processing like what gRPC does on HTTP/2 (will be kinda hacky on HTTP/1.1 since **technically** HTTP/1.1 does not support ping-ponging) or if you feel that the `send` function isn't good enough (which it probably is for non ping-pong related exchanges).

Through Option 2, we can create a websocket client at the given addresses. Note that the path is recommended to start with "ws://" and "wss://" and not with "http://" or "https://" (though I'll be forgiving and rewrite that http part to ws). Also note that whatever you set the method to... it doesn't matter, if its HTTP/1.1 it will send a GET request with an Upgrade header, if its HTTP/2 it will send a CONNECT request and set some websocket headers. Any headers set by the client which are "websocket" headers will be overwritten, anything which is not websocket related will be kept.

Through Option 3, we can have `fetch` or `axios` or `HTTPClient` like syntax for sending requests. You can create a function or a class which obeys the payload dispatcher and reference it or you can use some of the existing ones in the `payloads` namespace described in the `webcraft::web::core` section

### web_response_base

```cpp
class web_response_base
{
public:
    std::optional<std::string> get_response_header(std::string name);
    std::vector<std::string> get_response_headers(std::string name);
    std::unordered_map<std::string, std::vector<std::string>> get_response_headers();

    http_response_code get_response_code();
    http_method get_method();
    connection_protocol get_protocol();
    std::string get_remote_host();
    uint16_t get_remote_port();
};

```

### web_client_connection

```cpp
class web_client_connection : public web_response_base
{
public:
    web_read_stream auto& get_read_stream();
    web_write_stream auto& get_write_stream();
    task<void> close();
};
```

Represents a raw client connection. Allows the user to manage input and output stream processing directly.

### web_socket_connection

```cpp
class web_socket_connection : public web_response_base
{
public:
    web_read_stream& get_read_stream();
    web_write_stream& get_write_stream();
    task<void> close();
};
```

Represents a websocket connection. It looks like its the same as `web_client_connection` but its actually establishing an HTTP connection under the hood and then running the WebSocket protocol on top of it.

### web_response

```cpp
class web_response : public web_response_base
{
public:
    template<typename T>
    task<T> get_payload(payload_handler<T> auto&& handler);

    task<void> close();
};
```

Allows us to get the payload. You can create a function or a class which obeys the payload handler and reference it or you can use some of the existing ones in the `payloads` namespace described in the `webcraft::web::core` section.

## namespace webcraft::web::server

Web Clients are fun and necessary but clients are supposed to connect to something and thats what this framework helping you craft, web servers. So lets get into the real deal.

### web_server

The web server implementation is defined as follows:
```cpp
class web_server
{
public:
    // server properties
    bool add_supported_protocol(connection_protocol protocol);
    void set_ssl_context(ssl_context ctx);
    void set_property(std::string key, std::string value);
    // server runtime management functions
    task<void> start();
    fire_and_forget_task shutdown();
    task<void> run_until_stopped(std::stop_token token);

    // lifetime hooks
    void on_started(server_state_cb auto&& cb);
    void on_shutdown(server_state_cb auto&& cb);

    // routing
    web_route& route(std::vector<std::string> routes);
};
```

It supports setting initialization proeprties (constants defined in the namespace like "http.port" and "https.port" and "host"). It also allows asynchronously starting the program (start the server and have it run in the background). Once the server is started we can be notified by setting `on_started` callback. We can signal shutdown using the `shutdown` function and once our server is shutdown, we can get a notification on the `on_shutdown` callback. Another approach would be to have a stop source and have it run until the stop_token is fired (callbacks will still be fired, `shutdown` signal takes more priority than stop token passed in). To perform routing and serving, we can use the `route` function to provide us a representation of what happens in that route. 

### server_state_cb & web_server_context_manager

Our callbacks look like this. We are implementing bare minimum CDI since languages like JS & Python have features where in the middleware they can "attach" information to the request object. While you can bypass this by setting some special header internally and checking if thats good, this would be better to manage "server" scoped logic.

```cpp
template<typename T>
concept server_state_cb = requires(T t1, T& t2, T&& t3, web_server_context_manager manager) {
    { t1(manager) } -> std::same_as<task<void>>;
    { t2(manager) } -> std::same_as<task<void>>;
    { t3(manager) } -> std::same_as<task<void>>;
};

class web_server_context_manager
{
public:
    template<typename T>
    T get(std::string item);
    
    template<typename T>
    void set(std::string item, T value);
};
```

### web_route & error_handler_route & cors_config

The web route represents the your actual route. Middleware can be handled directly using the `use` function. You can also perform raw request handling (non websocket ping-ponging or custom protocol ontop of HTTP like gRPC) in the `use` function (I'd recommend you to do so here).

The route will be continoud to be "chained" until the user chooses to have it handle HTTP connections or WebSocket connections which it will then fallback on an error_handler_route. This error handler route can pass in an `error_handler_cb` in which if some exception happens along the chain, this route is the first responder (and our internal server stuff being the next).

Lastly, all API should be able to be visible on the brwoser side so CORS config has been added.

```cpp
class web_route
{
public:
    web_route& use(web_route_handler_cb handler); // for middleware or raw connections
    web_route& cors(cors_config config); // CORS handler
    error_handler_route handle_http_method(http_method method, http_route_handler_cb handler); // for basic http connections
    error_handler_route handle_websocket(web_socket_handler_cb handler); // for websocket connections
};

class error_handler_route
{
public:
    void on_error(error_handler auto&& handler);
}

struct cors_config
{
    std::vector<http_method> allowed_methods;
    std::vector<std::string> allowed_origins;
    std::vector<std::string> allowed_headers;
    bool allow_credentials;
};
```

### web_route_handler & http_route_handler & web_socket_handler & error_handler_cb

Our callbacks. I find its kind of self explainatory. `web_route_handler`, `web_socket_handler`, `error_handler_cb` both accept a connection scoped context, a request which provides a readable stream, and a response which provides a writable stream. `web_route_handler` also provides a `next_function` if one wants to move to the next chain of events. `http_route_handler` is different, it accepts a request which provides a payload and returns a response object. `error_handler_cb` provides an exception pointer object.

```cpp
using next_function = std::function<task<void>()>;

template<typename T>
concept web_route_handler_cb = requires(T t1, T& t2, T&& t3, web_server_context_view view, web_request req, web_response resp, next_function func)
{
    { t1(view, req, resp, func) } -> std::same_as<task<void>>;
    { t2(view, req, resp, func) } -> std::same_as<task<void>>;
    { t3(view, req, resp, func) } -> std::same_as<task<void>>;
};

template<typename T>
concept http_route_handler_cb = requires(T t1, T& t2, T&& t3, web_server_context_view view, http_request req)
{
    { t1(view, req, resp) } -> std::same_as<task<http_response>>;
    { t2(view, req, resp) } -> std::same_as<task<http_response>>;
    { t3(view, req, resp) } -> std::same_as<task<http_response>>;
};

template<typename T>
concept web_socket_handler_cb = requires(T t1, T& t2, T&& t3, web_server_context_view view, web_socket_request req, web_socket_response resp)
{
    { t1(view, req, resp) } -> std::same_as<task<void>>;
    { t2(view, req, resp) } -> std::same_as<task<void>>;
    { t3(view, req, resp) } -> std::same_as<task<void>>;
};

template<typename T>
concept error_handler_cb = requires(T t1, T& t2, T&& t3, std::exception_ptr ptr, web_server_context_view view, web_request req, web_response resp)
{
    { t1(ptr, view, req, resp) } -> std::same_as<task<void>>;
    { t2(ptr, view, req, resp) } -> std::same_as<task<void>>;
    { t3(ptr, view, req, resp) } -> std::same_as<task<void>>;
};
```

### web_server_context_view

This is how connection scoped CDI looks like.

```cpp
class web_server_context_view
{
public:
    template<typename T>
    T get_global(std::string item);
    template<typename T>
    void set_global(std::string item, T value);
    template<typename T>
    T get_local(std::string item);
    template<typename T>
    void set_local(std::string item, T value);
};
```

### Web Requests

This is the definition for the base of all web requests.

```cpp
class web_request_base
{
public:
    std::unordered_map<std::string, std::string> get_path_params();
    std::optional<std::string> get_path_param(std::string param);

    std::unordered_map<std::string, std::string> get_query_params();
    std::optional<std::string> get_query_param(std::string param);

    std::string get_path();
    std::string get_http_method();

    std::unordered_map<std::string, std::vector<std::string>> get_headers();
    std::vector<std::string> get_headers(std::string name);
    std::optional<std::string> get_headers(std::string name);
};
```

These are the individual request types.

```cpp
class web_request : public web_request_base
{
public:
    web_read_stream auto& get_read_stream();

};
```

```cpp
class web_socket_request : public web_request_base
{
public:
    web_read_stream auto& get_read_stream();
};
```

```cpp
class http_request : public web_request_base
{
public:
    template <typename T>
    task<T> get_payload(payload_handler<T> auto&& handler);
};
```

### Web Responses

This is the definition for the base of all web responses.

```cpp
class web_response_base
{
public:
    task<void> set_status_code(http_code code);
    task<void> set_header(std::string name, std::string value);
};
```

These are the individual response types.

```cpp
class web_response : public web_response_base
{
public:
    web_write_stream auto& get_write_stream();
};
```

```cpp
class web_socket_response : public web_response_base
{
public:
    web_write_stream auto& get_write_stream();
};
```

```cpp
class http_response
{
private:
    friend class web_server;

    http_response& set_response_code(http_code code);
    http_response& set_header(std::string name, std::string header);
    http_response& set_headers(std::unordered_map<std::string, std::vector<std::string>> headers);
    http_response& set_payload(payload_dispatcher auto&& dispatcher);
};
```

## namespace webcraft::web::secure

Coming Soon.... will need to add OpenSSL into the mix.

## Resources

- https://developer.mozilla.org/en-US/docs/Web/HTTP/Reference/Resources_and_specifications
- https://httpwg.org/specs/
- https://websocket.org/guides/websocket-protocol/
- https://websockets.spec.whatwg.org/
- https://developer.mozilla.org/en-US/docs/Web/API/WebSockets_API/Writing_WebSocket_servers
- https://www.rfc-editor.org/rfc/rfc7118.html