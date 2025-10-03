///////////////////////////////////////////////////////////////////////////////
// Copyright (c) Aditya Rao
// Licenced under MIT license. See LICENSE.txt for details.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include <utility>
#include <span>
#include <expected>

#include <webcraft/async/async.hpp>

namespace webcraft::web::core
{
    // web streams
    template <typename T>
    concept web_read_stream = webcraft::async::io::async_buffered_readable_stream<T, char> && webcraft::async::io::async_closeable_stream<T, char>;

    template <typename T>
    concept web_write_stream = webcraft::async::io::async_buffered_writable_stream<T, const char> && webcraft::async::io::async_closeable_stream<T, const char>;

    // HTTP methods
    enum class http_method
    {
        GET,
        HEAD,
        POST,
        PUT,
        DELETE,
        CONNECT,
        OPTIONS,
        TRACE,
        PATCH
    };

    // Helper functions
    constexpr std::string_view to_string(http_method method) noexcept
    {
        switch (method)
        {
        case http_method::GET:
            return "GET";
        case http_method::HEAD:
            return "HEAD";
        case http_method::POST:
            return "POST";
        case http_method::PUT:
            return "PUT";
        case http_method::DELETE:
            return "DELETE";
        case http_method::CONNECT:
            return "CONNECT";
        case http_method::OPTIONS:
            return "OPTIONS";
        case http_method::TRACE:
            return "TRACE";
        case http_method::PATCH:
            return "PATCH";
        }
        return "UNKNOWN";
    }

    constexpr std::optional<http_method> from_string(std::string_view str) noexcept
    {
        if (str == "GET")
            return http_method::GET;
        if (str == "HEAD")
            return http_method::HEAD;
        if (str == "POST")
            return http_method::POST;
        if (str == "PUT")
            return http_method::PUT;
        if (str == "DELETE")
            return http_method::DELETE;
        if (str == "CONNECT")
            return http_method::CONNECT;
        if (str == "OPTIONS")
            return http_method::OPTIONS;
        if (str == "TRACE")
            return http_method::TRACE;
        if (str == "PATCH")
            return http_method::PATCH;
        return std::nullopt;
    }

    // status codes
    namespace response_code
    {
        // 1xx Informational responses
        inline constexpr int CONTINUE = 100;
        inline constexpr int SWITCHING_PROTOCOLS = 101;
        inline constexpr int PROCESSING = 102;
        inline constexpr int EARLY_HINTS = 103;

        // 2xx Success
        inline constexpr int OK = 200;
        inline constexpr int CREATED = 201;
        inline constexpr int ACCEPTED = 202;
        inline constexpr int NON_AUTHORITATIVE_INFORMATION = 203;
        inline constexpr int NO_CONTENT = 204;
        inline constexpr int RESET_CONTENT = 205;
        inline constexpr int PARTIAL_CONTENT = 206;
        inline constexpr int MULTI_STATUS = 207;
        inline constexpr int ALREADY_REPORTED = 208;
        inline constexpr int IM_USED = 226;

        // 3xx Redirection
        inline constexpr int MULTIPLE_CHOICES = 300;
        inline constexpr int MOVED_PERMANENTLY = 301;
        inline constexpr int FOUND = 302;
        inline constexpr int SEE_OTHER = 303;
        inline constexpr int NOT_MODIFIED = 304;
        inline constexpr int USE_PROXY = 305;
        inline constexpr int TEMPORARY_REDIRECT = 307;
        inline constexpr int PERMANENT_REDIRECT = 308;

        // 4xx Client errors
        inline constexpr int BAD_REQUEST = 400;
        inline constexpr int UNAUTHORIZED = 401;
        inline constexpr int PAYMENT_REQUIRED = 402;
        inline constexpr int FORBIDDEN = 403;
        inline constexpr int NOT_FOUND = 404;
        inline constexpr int METHOD_NOT_ALLOWED = 405;
        inline constexpr int NOT_ACCEPTABLE = 406;
        inline constexpr int PROXY_AUTHENTICATION_REQUIRED = 407;
        inline constexpr int REQUEST_TIMEOUT = 408;
        inline constexpr int CONFLICT = 409;
        inline constexpr int GONE = 410;
        inline constexpr int LENGTH_REQUIRED = 411;
        inline constexpr int PRECONDITION_FAILED = 412;
        inline constexpr int PAYLOAD_TOO_LARGE = 413;
        inline constexpr int URI_TOO_LONG = 414;
        inline constexpr int UNSUPPORTED_MEDIA_TYPE = 415;
        inline constexpr int RANGE_NOT_SATISFIABLE = 416;
        inline constexpr int EXPECTATION_FAILED = 417;
        inline constexpr int IM_A_TEAPOT = 418;
        inline constexpr int MISDIRECTED_REQUEST = 421;
        inline constexpr int UNPROCESSABLE_ENTITY = 422;
        inline constexpr int LOCKED = 423;
        inline constexpr int FAILED_DEPENDENCY = 424;
        inline constexpr int TOO_EARLY = 425;
        inline constexpr int UPGRADE_REQUIRED = 426;
        inline constexpr int PRECONDITION_REQUIRED = 428;
        inline constexpr int TOO_MANY_REQUESTS = 429;
        inline constexpr int REQUEST_HEADER_FIELDS_TOO_LARGE = 431;
        inline constexpr int UNAVAILABLE_FOR_LEGAL_REASONS = 451;

        // 5xx Server errors
        inline constexpr int INTERNAL_SERVER_ERROR = 500;
        inline constexpr int NOT_IMPLEMENTED = 501;
        inline constexpr int BAD_GATEWAY = 502;
        inline constexpr int SERVICE_UNAVAILABLE = 503;
        inline constexpr int GATEWAY_TIMEOUT = 504;
        inline constexpr int HTTP_VERSION_NOT_SUPPORTED = 505;
        inline constexpr int VARIANT_ALSO_NEGOTIATES = 506;
        inline constexpr int INSUFFICIENT_STORAGE = 507;
        inline constexpr int LOOP_DETECTED = 508;
        inline constexpr int NOT_EXTENDED = 510;
        inline constexpr int NETWORK_AUTHENTICATION_REQUIRED = 511;
    }

    // Helper functions for status codes
    constexpr std::string_view status_text(int code) noexcept
    {
        switch (code)
        {
        // 1xx
        case 100:
            return "Continue";
        case 101:
            return "Switching Protocols";
        case 102:
            return "Processing";
        case 103:
            return "Early Hints";

        // 2xx
        case 200:
            return "OK";
        case 201:
            return "Created";
        case 202:
            return "Accepted";
        case 203:
            return "Non-Authoritative Information";
        case 204:
            return "No Content";
        case 205:
            return "Reset Content";
        case 206:
            return "Partial Content";
        case 207:
            return "Multi-Status";
        case 208:
            return "Already Reported";
        case 226:
            return "IM Used";

        // 3xx
        case 300:
            return "Multiple Choices";
        case 301:
            return "Moved Permanently";
        case 302:
            return "Found";
        case 303:
            return "See Other";
        case 304:
            return "Not Modified";
        case 305:
            return "Use Proxy";
        case 307:
            return "Temporary Redirect";
        case 308:
            return "Permanent Redirect";

        // 4xx
        case 400:
            return "Bad Request";
        case 401:
            return "Unauthorized";
        case 402:
            return "Payment Required";
        case 403:
            return "Forbidden";
        case 404:
            return "Not Found";
        case 405:
            return "Method Not Allowed";
        case 406:
            return "Not Acceptable";
        case 407:
            return "Proxy Authentication Required";
        case 408:
            return "Request Timeout";
        case 409:
            return "Conflict";
        case 410:
            return "Gone";
        case 411:
            return "Length Required";
        case 412:
            return "Precondition Failed";
        case 413:
            return "Payload Too Large";
        case 414:
            return "URI Too Long";
        case 415:
            return "Unsupported Media Type";
        case 416:
            return "Range Not Satisfiable";
        case 417:
            return "Expectation Failed";
        case 418:
            return "I'm a teapot";
        case 421:
            return "Misdirected Request";
        case 422:
            return "Unprocessable Entity";
        case 423:
            return "Locked";
        case 424:
            return "Failed Dependency";
        case 425:
            return "Too Early";
        case 426:
            return "Upgrade Required";
        case 428:
            return "Precondition Required";
        case 429:
            return "Too Many Requests";
        case 431:
            return "Request Header Fields Too Large";
        case 451:
            return "Unavailable For Legal Reasons";

        // 5xx
        case 500:
            return "Internal Server Error";
        case 501:
            return "Not Implemented";
        case 502:
            return "Bad Gateway";
        case 503:
            return "Service Unavailable";
        case 504:
            return "Gateway Timeout";
        case 505:
            return "HTTP Version Not Supported";
        case 506:
            return "Variant Also Negotiates";
        case 507:
            return "Insufficient Storage";
        case 508:
            return "Loop Detected";
        case 510:
            return "Not Extended";
        case 511:
            return "Network Authentication Required";

        default:
            return "Unknown";
        }
    }

    constexpr bool is_informational(int code) noexcept { return code >= 100 && code < 200; }
    constexpr bool is_success(int code) noexcept { return code >= 200 && code < 300; }
    constexpr bool is_redirection(int code) noexcept { return code >= 300 && code < 400; }
    constexpr bool is_client_error(int code) noexcept { return code >= 400 && code < 500; }
    constexpr bool is_server_error(int code) noexcept { return code >= 500 && code < 600; }
    constexpr bool is_error(int code) noexcept { return code >= 400; }

    // payload handlers
    template <typename T, typename RStream>
    concept payload_dispatcher = web_write_stream<RStream> && requires(T t1, T &t2, T &&t3, RStream &stream) {
        { t1(stream) } -> std::same_as<async_t(void)>;
        { t2(stream) } -> std::same_as<async_t(void)>;
        { t3(stream) } -> std::same_as<async_t(void)>;
    };

    template <typename T, typename Ret, typename WStream>
    concept payload_handler = web_read_stream<WStream> && requires(T t1, T &t2, T &&t3, WStream &stream) {
        { t1(stream) } -> std::same_as<async_t(Ret)>;
        { t2(stream) } -> std::same_as<async_t(Ret)>;
        { t3(stream) } -> std::same_as<async_t(Ret)>;
    };

}

namespace webcraft::web::connection
{
    // connections
    enum class connection_protocol
    {
        HTTP_1_0,
        HTTP_1_1,
        HTTP_2,
        HTTP_3
    };

    class connection
    {
    public:
        virtual ~connection() = default;

        virtual async_t(size_t) send_data(const std::span<const char> data) = 0;
        virtual async_t(size_t) receive_data(const std::span<char> data) = 0;
        virtual connection_protocol get_protocol() const noexcept = 0;
        virtual std::string get_remote_address() const noexcept = 0;
        virtual uint16_t get_remote_port() const noexcept = 0;

        virtual async_t(void) close() = 0;
        virtual async_t(void) shutdown() = 0;
    };

    class connection_provider
    {
    public:
        virtual ~connection_provider() = default;

        virtual async_t(std::shared_ptr<connection>) get_connection() = 0;
        virtual std::vector<connection_protocol> get_supported_protocols() = 0;
    };
}

namespace webcraft::web::headers
{
    // General Headers
    constexpr std::string_view CACHE_CONTROL = "Cache-Control";
    constexpr std::string_view CONNECTION = "Connection";
    constexpr std::string_view DATE = "Date";
    constexpr std::string_view PRAGMA = "Pragma";
    constexpr std::string_view TRAILER = "Trailer";
    constexpr std::string_view TRANSFER_ENCODING = "Transfer-Encoding";
    constexpr std::string_view UPGRADE = "Upgrade";
    constexpr std::string_view VIA = "Via";
    constexpr std::string_view WARNING = "Warning";

    // Request Headers
    constexpr std::string_view ACCEPT = "Accept";
    constexpr std::string_view ACCEPT_CHARSET = "Accept-Charset";
    constexpr std::string_view ACCEPT_ENCODING = "Accept-Encoding";
    constexpr std::string_view ACCEPT_LANGUAGE = "Accept-Language";
    constexpr std::string_view ACCEPT_DATETIME = "Accept-Datetime";
    constexpr std::string_view AUTHORIZATION = "Authorization";
    constexpr std::string_view COOKIE = "Cookie";
    constexpr std::string_view EXPECT = "Expect";
    constexpr std::string_view FORWARDED = "Forwarded";
    constexpr std::string_view FROM = "From";
    constexpr std::string_view HOST = "Host";
    constexpr std::string_view IF_MATCH = "If-Match";
    constexpr std::string_view IF_MODIFIED_SINCE = "If-Modified-Since";
    constexpr std::string_view IF_NONE_MATCH = "If-None-Match";
    constexpr std::string_view IF_RANGE = "If-Range";
    constexpr std::string_view IF_UNMODIFIED_SINCE = "If-Unmodified-Since";
    constexpr std::string_view MAX_FORWARDS = "Max-Forwards";
    constexpr std::string_view ORIGIN = "Origin";
    constexpr std::string_view PROXY_AUTHORIZATION = "Proxy-Authorization";
    constexpr std::string_view RANGE = "Range";
    constexpr std::string_view REFERER = "Referer";
    constexpr std::string_view TE = "TE";
    constexpr std::string_view USER_AGENT = "User-Agent";

    // Response Headers
    constexpr std::string_view ACCEPT_RANGES = "Accept-Ranges";
    constexpr std::string_view AGE = "Age";
    constexpr std::string_view ETAG = "ETag";
    constexpr std::string_view LOCATION = "Location";
    constexpr std::string_view PROXY_AUTHENTICATE = "Proxy-Authenticate";
    constexpr std::string_view RETRY_AFTER = "Retry-After";
    constexpr std::string_view SERVER = "Server";
    constexpr std::string_view SET_COOKIE = "Set-Cookie";
    constexpr std::string_view VARY = "Vary";
    constexpr std::string_view WWW_AUTHENTICATE = "WWW-Authenticate";

    // Representation Headers
    constexpr std::string_view CONTENT_ENCODING = "Content-Encoding";
    constexpr std::string_view CONTENT_LANGUAGE = "Content-Language";
    constexpr std::string_view CONTENT_LENGTH = "Content-Length";
    constexpr std::string_view CONTENT_LOCATION = "Content-Location";
    constexpr std::string_view CONTENT_TYPE = "Content-Type";
    constexpr std::string_view CONTENT_RANGE = "Content-Range";
    constexpr std::string_view CONTENT_DISPOSITION = "Content-Disposition";
    constexpr std::string_view LAST_MODIFIED = "Last-Modified";

    // CORS Headers
    constexpr std::string_view ACCESS_CONTROL_ALLOW_ORIGIN = "Access-Control-Allow-Origin";
    constexpr std::string_view ACCESS_CONTROL_ALLOW_CREDENTIALS = "Access-Control-Allow-Credentials";
    constexpr std::string_view ACCESS_CONTROL_ALLOW_HEADERS = "Access-Control-Allow-Headers";
    constexpr std::string_view ACCESS_CONTROL_ALLOW_METHODS = "Access-Control-Allow-Methods";
    constexpr std::string_view ACCESS_CONTROL_EXPOSE_HEADERS = "Access-Control-Expose-Headers";
    constexpr std::string_view ACCESS_CONTROL_MAX_AGE = "Access-Control-Max-Age";
    constexpr std::string_view ACCESS_CONTROL_REQUEST_HEADERS = "Access-Control-Request-Headers";
    constexpr std::string_view ACCESS_CONTROL_REQUEST_METHOD = "Access-Control-Request-Method";

    // Security Headers
    constexpr std::string_view CONTENT_SECURITY_POLICY = "Content-Security-Policy";
    constexpr std::string_view CONTENT_SECURITY_POLICY_REPORT_ONLY = "Content-Security-Policy-Report-Only";
    constexpr std::string_view CROSS_ORIGIN_EMBEDDER_POLICY = "Cross-Origin-Embedder-Policy";
    constexpr std::string_view CROSS_ORIGIN_OPENER_POLICY = "Cross-Origin-Opener-Policy";
    constexpr std::string_view CROSS_ORIGIN_RESOURCE_POLICY = "Cross-Origin-Resource-Policy";
    constexpr std::string_view EXPECT_CT = "Expect-CT";
    constexpr std::string_view FEATURE_POLICY = "Feature-Policy";
    constexpr std::string_view PERMISSIONS_POLICY = "Permissions-Policy";
    constexpr std::string_view PUBLIC_KEY_PINS = "Public-Key-Pins";
    constexpr std::string_view PUBLIC_KEY_PINS_REPORT_ONLY = "Public-Key-Pins-Report-Only";
    constexpr std::string_view REFERRER_POLICY = "Referrer-Policy";
    constexpr std::string_view STRICT_TRANSPORT_SECURITY = "Strict-Transport-Security";
    constexpr std::string_view X_CONTENT_TYPE_OPTIONS = "X-Content-Type-Options";
    constexpr std::string_view X_DNS_PREFETCH_CONTROL = "X-DNS-Prefetch-Control";
    constexpr std::string_view X_FRAME_OPTIONS = "X-Frame-Options";
    constexpr std::string_view X_PERMITTED_CROSS_DOMAIN_POLICIES = "X-Permitted-Cross-Domain-Policies";
    constexpr std::string_view X_XSS_PROTECTION = "X-XSS-Protection";

    // WebSocket Headers
    constexpr std::string_view SEC_WEBSOCKET_ACCEPT = "Sec-WebSocket-Accept";
    constexpr std::string_view SEC_WEBSOCKET_EXTENSIONS = "Sec-WebSocket-Extensions";
    constexpr std::string_view SEC_WEBSOCKET_KEY = "Sec-WebSocket-Key";
    constexpr std::string_view SEC_WEBSOCKET_PROTOCOL = "Sec-WebSocket-Protocol";
    constexpr std::string_view SEC_WEBSOCKET_VERSION = "Sec-WebSocket-Version";

    // Fetch Metadata Headers
    constexpr std::string_view SEC_FETCH_DEST = "Sec-Fetch-Dest";
    constexpr std::string_view SEC_FETCH_MODE = "Sec-Fetch-Mode";
    constexpr std::string_view SEC_FETCH_SITE = "Sec-Fetch-Site";
    constexpr std::string_view SEC_FETCH_USER = "Sec-Fetch-User";

    // Server-Sent Events
    constexpr std::string_view LAST_EVENT_ID = "Last-Event-ID";

    // Do Not Track
    constexpr std::string_view DNT = "DNT";
    constexpr std::string_view TK = "Tk";

    // Downloads
    constexpr std::string_view X_DOWNLOAD_OPTIONS = "X-Download-Options";

    // Alt-Svc
    constexpr std::string_view ALT_SVC = "Alt-Svc";

    // Client Hints
    constexpr std::string_view ACCEPT_CH = "Accept-CH";
    constexpr std::string_view ACCEPT_CH_LIFETIME = "Accept-CH-Lifetime";
    constexpr std::string_view DEVICE_MEMORY = "Device-Memory";
    constexpr std::string_view DPR = "DPR";
    constexpr std::string_view VIEWPORT_WIDTH = "Viewport-Width";
    constexpr std::string_view WIDTH = "Width";

    // Non-standard but common headers
    constexpr std::string_view X_FORWARDED_FOR = "X-Forwarded-For";
    constexpr std::string_view X_FORWARDED_HOST = "X-Forwarded-Host";
    constexpr std::string_view X_FORWARDED_PROTO = "X-Forwarded-Proto";
    constexpr std::string_view X_REAL_IP = "X-Real-IP";
    constexpr std::string_view X_REQUEST_ID = "X-Request-ID";
    constexpr std::string_view X_CORRELATION_ID = "X-Correlation-ID";
    constexpr std::string_view X_POWERED_BY = "X-Powered-By";
    constexpr std::string_view X_UA_COMPATIBLE = "X-UA-Compatible";
    constexpr std::string_view X_HTTP_METHOD_OVERRIDE = "X-Http-Method-Override";
    constexpr std::string_view X_CLUSTER_CLIENT_IP = "X-Cluster-Client-IP";
    constexpr std::string_view FRONTEND_HTTPS = "Front-End-Https";
    constexpr std::string_view PROXY_CONNECTION = "Proxy-Connection";
    constexpr std::string_view X_ATT_DEVICEID = "X-ATT-DeviceId";
    constexpr std::string_view X_WAP_PROFILE = "X-Wap-Profile";

    // Additional experimental/proposed headers
    constexpr std::string_view CLEAR_SITE_DATA = "Clear-Site-Data";
    constexpr std::string_view CRITICAL_CH = "Critical-CH";
    constexpr std::string_view EARLY_DATA = "Early-Data";
    constexpr std::string_view LARGE_ALLOCATION = "Large-Allocation";
    constexpr std::string_view NEL = "NEL";
    constexpr std::string_view ORIGIN_ISOLATION = "Origin-Isolation";
    constexpr std::string_view REPORT_TO = "Report-To";
    constexpr std::string_view SEC_CH_PREFERS_COLOR_SCHEME = "Sec-CH-Prefers-Color-Scheme";
    constexpr std::string_view SEC_CH_PREFERS_REDUCED_MOTION = "Sec-CH-Prefers-Reduced-Motion";
    constexpr std::string_view SEC_CH_UA = "Sec-CH-UA";
    constexpr std::string_view SEC_CH_UA_ARCH = "Sec-CH-UA-Arch";
    constexpr std::string_view SEC_CH_UA_BITNESS = "Sec-CH-UA-Bitness";
    constexpr std::string_view SEC_CH_UA_FULL_VERSION = "Sec-CH-UA-Full-Version";
    constexpr std::string_view SEC_CH_UA_FULL_VERSION_LIST = "Sec-CH-UA-Full-Version-List";
    constexpr std::string_view SEC_CH_UA_MOBILE = "Sec-CH-UA-Mobile";
    constexpr std::string_view SEC_CH_UA_MODEL = "Sec-CH-UA-Model";
    constexpr std::string_view SEC_CH_UA_PLATFORM = "Sec-CH-UA-Platform";
    constexpr std::string_view SEC_CH_UA_PLATFORM_VERSION = "Sec-CH-UA-Platform-Version";
    constexpr std::string_view SEC_PURPOSE = "Sec-Purpose";
    constexpr std::string_view SERVICE_WORKER_NAVIGATION_PRELOAD = "Service-Worker-Navigation-Preload";
    constexpr std::string_view TIMING_ALLOW_ORIGIN = "Timing-Allow-Origin";
    constexpr std::string_view X_ROBOTS_TAG = "X-Robots-Tag";
}

namespace webcraft::web::payloads
{

    // empty payload handlers and dispatchers
    using empty = std::monostate;

    inline constexpr auto empty_payload = [](webcraft::web::core::web_write_stream auto & /*stream*/) -> async_t(void)
    {
        co_return;
    };

    inline constexpr auto ignore_payload = [](webcraft::web::core::web_read_stream auto & /*stream*/) -> async_t(empty)
    {
        co_return empty{};
    };

    // string payload
    inline constexpr auto handle_string_payload()
    {
        return [](webcraft::web::core::web_read_stream auto &stream) -> async_t(std::string)
        {
            std::vector<char> content;

            std::array<char, 4096> buffer{};
            size_t n;
            while ((n = co_await stream.recv(buffer)) > 0)
            {
                content.insert(content.end(), buffer.data(), buffer.data() + n);
            }

            co_return std::string(content.data(), content.size());
        };
    }

    inline constexpr auto dispatch_string_payload(std::string_view data)
    {
        return [data](webcraft::web::core::web_write_stream auto &stream) -> async_t(void)
        {
            co_await stream.send(std::span<const char>(data.data(), data.size()));
        };
    }

    // vector payload
    inline constexpr auto handle_vector_payload()
    {
        return [](webcraft::web::core::web_read_stream auto &stream) -> async_t(std::vector<char>)
        {
            std::vector<char> content;

            std::array<char, 4096> buffer{};
            size_t n;
            while ((n = co_await stream.recv(buffer)) > 0)
            {
                content.insert(content.end(), buffer.data(), buffer.data() + n);
            }

            co_return content;
        };
    }

    inline constexpr auto dispatch_vector_payload(const std::vector<char> &data)
    {
        return [data](webcraft::web::core::web_write_stream auto &stream) -> async_t(void)
        {
            co_await stream.send(std::span<const char>(data.data(), data.size()));
        };
    }

    template <webcraft::web::core::web_read_stream T>
    auto create_wrapper_read_stream(T &inner_stream)
    {
        class wrapper_read_stream
        {
        private:
            T *inner_stream;

        public:
            explicit wrapper_read_stream(T &stream) : inner_stream(&stream) {}

            wrapper_read_stream(const wrapper_read_stream &) = delete;
            wrapper_read_stream &operator=(const wrapper_read_stream &) = delete;

            wrapper_read_stream(wrapper_read_stream &&other) noexcept : inner_stream(std::exchange(other.inner_stream, nullptr)) {}
            wrapper_read_stream &operator=(wrapper_read_stream &&other) noexcept
            {
                if (this != &other)
                {
                    inner_stream = std::exchange(other.inner_stream, nullptr);
                }
                return *this;
            }

            async_t(size_t) recv(const std::span<char> buffer)
            {
                co_return co_await inner_stream->recv(buffer);
            }

            async_t(std::optional<char>) recv()
            {
                co_return co_await inner_stream->recv();
            }

            async_t(void) close()
            {
                co_await inner_stream->close();
            }
        };

        return wrapper_read_stream{inner_stream};
    }

    // stream payloads
    inline constexpr auto handle_stream_payload()
    {
        return [](webcraft::web::core::web_read_stream auto &stream) -> async_t(decltype(create_wrapper_read_stream(stream)))
        {
            co_return create_wrapper_read_stream(stream);
        };
    }

    inline constexpr auto dispatch_stream_payload(webcraft::web::core::web_read_stream auto &data)
    {
        return [&data](webcraft::web::core::web_write_stream auto &stream) -> async_t(void)
        {
            std::array<char, 4096> buffer{};
            size_t n;
            while ((n = co_await data.recv(buffer)) > 0)
            {
                co_await stream.send(std::span<const char>(buffer.data(), n));
            }
            co_return;
        };
    }
}