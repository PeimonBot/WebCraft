///////////////////////////////////////////////////////////////////////////////
// Copyright (c) Aditya Rao
// Licenced under MIT license. See LICENSE.txt for details.
///////////////////////////////////////////////////////////////////////////////

#define TEST_SUITE_NAME WebTests

#include "test_suite.hpp"
#include <webcraft/web/core.hpp>

using namespace webcraft::async;
using namespace webcraft::async::io;

struct mock_web_read_stream
{
private:
    std::vector<char> data;

public:
    task<std::optional<char>> recv()
    {
        if (data.empty())
        {
            co_return std::nullopt;
        }
        char value = data.front();
        data.erase(data.begin());
        co_return value;
    }

    task<size_t> recv(const std::span<char> buffer)
    {
        if (data.empty())
        {
            co_return 0;
        }
        size_t bytes_read = 0;
        for (char &c : buffer)
        {
            if (data.empty())
            {
                break;
            }
            c = data.front();
            data.erase(data.begin());
            bytes_read++;
        }
        co_return bytes_read;
    }

    task<void> close()
    {
        co_return;
    }

    void send(const std::string &str)
    {
        data.insert(data.end(), str.begin(), str.end());
    }

    std::string_view available_data() const
    {
        return std::string_view(data.data(), data.size());
    }
};

struct mock_web_write_stream
{
private:
    std::vector<char> data;

public:
    task<bool> send(const char value)
    {
        data.push_back(value);
        co_return true;
    }

    task<size_t> send(const std::span<const char> buffer)
    {
        for (char c : buffer)
        {
            data.push_back(c);
        }
        co_return buffer.size();
    }

    task<void> close()
    {
        co_return;
    }

    std::string_view available_data() const
    {
        return std::string_view(data.data(), data.size());
    }
};

static_assert(async_readable_stream<mock_web_read_stream, char>, "mock_web_read_stream should be an async readable stream");
static_assert(async_writable_stream<mock_web_write_stream, char>, "mock_web_write_stream should be an async writable stream");

static_assert(webcraft::web::core::payload_dispatcher<decltype(webcraft::web::payloads::empty_payload), mock_web_write_stream &>);
static_assert(webcraft::web::core::payload_handler<decltype(webcraft::web::payloads::ignore_payload), webcraft::web::payloads::empty, mock_web_read_stream &>);

constexpr auto data = "Hello, World!";

TEST_CASE(TestHandleEmptyPayload)
{
    mock_web_read_stream read_stream;
    read_stream.send(data);
    EXPECT_EQ(read_stream.available_data(), data);

    auto task_fn = co_async
    {
        auto handler = webcraft::web::payloads::ignore_payload;
        auto result = co_await handler(read_stream);
        static_assert(std::is_same_v<decltype(result), webcraft::web::payloads::empty>);
    };

    sync_wait(task_fn());
    EXPECT_EQ(read_stream.available_data(), data);
}

TEST_CASE(TestDispatchEmptyPayload)
{
    mock_web_write_stream write_stream;
    EXPECT_EQ(write_stream.available_data(), "");

    auto task_fn = co_async
    {
        auto dispatcher = webcraft::web::payloads::empty_payload;
        co_await dispatcher(write_stream);
    };

    sync_wait(task_fn());
    EXPECT_EQ(write_stream.available_data(), "");
}

static_assert(webcraft::web::core::payload_dispatcher<decltype(webcraft::web::payloads::dispatch_string_payload("")), mock_web_write_stream &>);
static_assert(webcraft::web::core::payload_handler<decltype(webcraft::web::payloads::handle_string_payload()), std::string, mock_web_read_stream &>);

TEST_CASE(TestHandleStringPayload)
{
    mock_web_read_stream read_stream;
    read_stream.send(data);
    EXPECT_EQ(read_stream.available_data(), data);

    auto task_fn = co_async
    {
        auto handler = webcraft::web::payloads::handle_string_payload();
        auto result = co_await handler(read_stream);
        static_assert(std::is_same_v<decltype(result), std::string>);
        EXPECT_EQ(result, data);
        co_return;
    };

    sync_wait(task_fn());
    EXPECT_EQ(read_stream.available_data(), "");
}

TEST_CASE(TestDispatchStringPayload)
{
    mock_web_write_stream write_stream;

    EXPECT_EQ(write_stream.available_data(), "");
    auto task_fn = co_async
    {
        auto dispatcher = webcraft::web::payloads::dispatch_string_payload(data);
        co_await dispatcher(write_stream);
    };

    sync_wait(task_fn());
    EXPECT_EQ(write_stream.available_data(), data);
}

static_assert(webcraft::web::core::payload_dispatcher<decltype(webcraft::web::payloads::dispatch_vector_payload(std::declval<std::vector<char>>())), mock_web_write_stream &>);
static_assert(webcraft::web::core::payload_handler<decltype(webcraft::web::payloads::handle_vector_payload()), std::vector<char>, mock_web_read_stream &>);

TEST_CASE(TestHandleVectorPayload)
{
    mock_web_read_stream read_stream;
    read_stream.send(data);
    EXPECT_EQ(read_stream.available_data(), data);

    auto task_fn = co_async
    {
        auto handler = webcraft::web::payloads::handle_vector_payload();
        auto result = co_await handler(read_stream);
        static_assert(std::is_same_v<decltype(result), std::vector<char>>);
        EXPECT_EQ(std::string(result.data(), result.size()), data);
        co_return;
    };

    sync_wait(task_fn());
    EXPECT_EQ(read_stream.available_data(), "");
}

TEST_CASE(TestDispatchVectorPayload)
{
    mock_web_write_stream write_stream;

    EXPECT_EQ(write_stream.available_data(), "");
    auto task_fn = co_async
    {
        auto dispatcher = webcraft::web::payloads::dispatch_vector_payload(std::vector<char>(data, data + strlen(data)));
        co_await dispatcher(write_stream);
    };

    sync_wait(task_fn());
    EXPECT_EQ(write_stream.available_data(), data);
}

static_assert(webcraft::web::core::payload_dispatcher<decltype(webcraft::web::payloads::dispatch_stream_payload(std::declval<mock_web_read_stream &>())), mock_web_write_stream &>);
static_assert(webcraft::web::core::payload_handler<decltype(webcraft::web::payloads::handle_stream_payload()), decltype(webcraft::web::payloads::create_wrapper_read_stream(std::declval<mock_web_read_stream &>())), mock_web_read_stream &>);

TEST_CASE(TestHandleStreamPayload)
{
    mock_web_read_stream read_stream;
    read_stream.send(data);
    EXPECT_EQ(read_stream.available_data(), data);

    auto task_fn = co_async
    {
        auto handler = webcraft::web::payloads::handle_stream_payload();
        auto result = co_await handler(read_stream);
        static_assert(std::is_same_v<decltype(result), decltype(webcraft::web::payloads::create_wrapper_read_stream(read_stream))>);
        std::string received_data;
        std::optional<char> ch;
        while ((ch = co_await result.recv()))
        {
            received_data.push_back(*ch);
        }
        EXPECT_EQ(received_data, data);
        co_return;
    };

    sync_wait(task_fn());
    EXPECT_EQ(read_stream.available_data(), "");
}

TEST_CASE(TestDispatchStreamPayload)
{
    mock_web_read_stream read_stream;
    read_stream.send(data);
    EXPECT_EQ(read_stream.available_data(), data);

    mock_web_write_stream write_stream;
    EXPECT_EQ(write_stream.available_data(), "");

    auto task_fn = co_async
    {
        auto dispatcher = webcraft::web::payloads::dispatch_stream_payload(read_stream);
        co_await dispatcher(write_stream);
    };

    sync_wait(task_fn());
    EXPECT_EQ(write_stream.available_data(), data);
    EXPECT_EQ(read_stream.available_data(), "");
}

// URI Tests
TEST_CASE(TestUriParseSimpleHttp)
{
    auto result = webcraft::web::core::uri::parse("http://example.com");
    EXPECT_TRUE(result.has_value());

    auto uri = result.value();
    EXPECT_TRUE(uri.is_absolute());
    EXPECT_FALSE(uri.is_relative());
    EXPECT_TRUE(uri.is_hierarchical());
    EXPECT_FALSE(uri.is_opaque());
    EXPECT_TRUE(uri.is_server_based_hierarchical());

    EXPECT_EQ(uri.scheme().value(), "http");
    EXPECT_EQ(uri.host().value(), "example.com");
    EXPECT_EQ(uri.authority().value(), "example.com");
    EXPECT_EQ(uri.path().value(), "");
    EXPECT_FALSE(uri.port().has_value());
    EXPECT_FALSE(uri.query().has_value());
    EXPECT_FALSE(uri.fragment().has_value());
    EXPECT_FALSE(uri.userinfo().has_value());
}

TEST_CASE(TestUriParseCompleteUri)
{
    auto result = webcraft::web::core::uri::parse("https://user:pass@example.com:8080/path/to/resource?param1=value1&param2=value2#fragment");
    EXPECT_TRUE(result.has_value());

    auto uri = result.value();
    EXPECT_TRUE(uri.is_absolute());
    EXPECT_TRUE(uri.is_hierarchical());
    EXPECT_TRUE(uri.is_server_based_hierarchical());

    EXPECT_EQ(uri.scheme().value(), "https");
    EXPECT_EQ(uri.userinfo().value(), "user:pass");
    EXPECT_EQ(uri.host().value(), "example.com");
    EXPECT_EQ(uri.port().value(), 8080);
    EXPECT_EQ(uri.path().value(), "/path/to/resource");
    EXPECT_EQ(uri.query().value(), "param1=value1&param2=value2");
    EXPECT_EQ(uri.fragment().value(), "fragment");
    EXPECT_EQ(uri.authority().value(), "user:pass@example.com:8080");
}

TEST_CASE(TestUriParseRelativeUri)
{
    auto result = webcraft::web::core::uri::parse("/path/to/resource?query=value#fragment");
    EXPECT_TRUE(result.has_value());

    auto uri = result.value();
    EXPECT_FALSE(uri.is_absolute());
    EXPECT_TRUE(uri.is_relative());
    EXPECT_TRUE(uri.is_hierarchical());
    EXPECT_FALSE(uri.is_opaque());

    EXPECT_FALSE(uri.scheme().has_value());
    EXPECT_EQ(uri.path().value(), "/path/to/resource");
    EXPECT_EQ(uri.query().value(), "query=value");
    EXPECT_EQ(uri.fragment().value(), "fragment");
    EXPECT_FALSE(uri.authority().has_value());
}

TEST_CASE(TestUriParseOpaqueUri)
{
    auto result = webcraft::web::core::uri::parse("mailto:test@example.com");
    EXPECT_TRUE(result.has_value());

    auto uri = result.value();
    EXPECT_TRUE(uri.is_absolute());
    EXPECT_FALSE(uri.is_relative());
    EXPECT_FALSE(uri.is_hierarchical());
    EXPECT_TRUE(uri.is_opaque());

    EXPECT_EQ(uri.scheme().value(), "mailto");
    EXPECT_EQ(uri.scheme_specific_part(), "test@example.com");
    EXPECT_FALSE(uri.path().has_value());
    EXPECT_FALSE(uri.authority().has_value());
}

TEST_CASE(TestUriParseIpv6)
{
    auto result = webcraft::web::core::uri::parse("http://[2001:db8::1]:8080/path");
    EXPECT_TRUE(result.has_value());

    auto uri = result.value();
    EXPECT_TRUE(uri.is_server_based_hierarchical());

    EXPECT_EQ(uri.scheme().value(), "http");
    EXPECT_EQ(uri.host().value(), "[2001:db8::1]");
    EXPECT_EQ(uri.port().value(), 8080);
    EXPECT_EQ(uri.path().value(), "/path");
}

TEST_CASE(TestUriParseWithoutScheme)
{
    auto result = webcraft::web::core::uri::parse("//example.com/path");
    EXPECT_TRUE(result.has_value());

    auto uri = result.value();
    EXPECT_FALSE(uri.is_absolute());
    EXPECT_TRUE(uri.is_relative());
    EXPECT_TRUE(uri.is_hierarchical());

    EXPECT_FALSE(uri.scheme().has_value());
    EXPECT_EQ(uri.authority().value(), "example.com");
    EXPECT_EQ(uri.host().value(), "example.com");
    EXPECT_EQ(uri.path().value(), "/path");
}

TEST_CASE(TestUriEquality)
{
    auto uri1 = webcraft::web::core::uri::parse("http://example.com/path").value();
    auto uri2 = webcraft::web::core::uri::parse("http://example.com/path").value();
    auto uri3 = webcraft::web::core::uri::parse("https://example.com/path").value();

    EXPECT_TRUE(uri1 == uri2);
    EXPECT_FALSE(uri1 == uri3);
}

TEST_CASE(TestUriConversions)
{
    auto uri = webcraft::web::core::uri::parse("http://example.com/path").value();

    std::string_view sv = uri;
    std::string s = uri;

    EXPECT_EQ(sv, "http://example.com/path");
    EXPECT_EQ(s, "http://example.com/path");
}

// URI Builder Tests
TEST_CASE(TestUriBuilderBasic)
{
    auto result = webcraft::web::core::uri_builder()
                      .scheme("https")
                      .host("example.com")
                      .port(443)
                      .path("/api/v1")
                      .build();

    EXPECT_TRUE(result.has_value());
    auto uri = result.value();

    EXPECT_EQ(uri.scheme().value(), "https");
    EXPECT_EQ(uri.host().value(), "example.com");
    EXPECT_EQ(uri.port().value(), 443);
    EXPECT_EQ(uri.path().value(), "/api/v1");
}

TEST_CASE(TestUriBuilderComplete)
{
    auto result = webcraft::web::core::uri_builder()
                      .scheme("https")
                      .userinfo("user:pass")
                      .host("example.com")
                      .port(8080)
                      .path("/api")
                      .append_query_param("param1", "value1")
                      .append_query_param("param2", "value2")
                      .fragment("section1")
                      .build();

    EXPECT_TRUE(result.has_value());
    auto uri = result.value();

    EXPECT_EQ(uri.scheme().value(), "https");
    EXPECT_EQ(uri.userinfo().value(), "user:pass");
    EXPECT_EQ(uri.host().value(), "example.com");
    EXPECT_EQ(uri.port().value(), 8080);
    EXPECT_EQ(uri.path().value(), "/api");
    EXPECT_EQ(uri.query().value(), "param1=value1&param2=value2");
    EXPECT_EQ(uri.fragment().value(), "section1");
}

TEST_CASE(TestUriBuilderPathAppend)
{
    auto result = webcraft::web::core::uri_builder()
                      .scheme("http")
                      .host("example.com")
                      .append_path("api")
                      .append_path("v1")
                      .append_path("users")
                      .build();

    EXPECT_TRUE(result.has_value());
    auto uri = result.value();

    EXPECT_EQ(uri.path().value(), "/api/v1/users");
}

TEST_CASE(TestUriBuilderFromExistingUri)
{
    auto original = webcraft::web::core::uri::parse("http://example.com/path").value();

    auto result = webcraft::web::core::uri_builder(original)
                      .scheme("https")
                      .port(443)
                      .append_path("api")
                      .build();

    EXPECT_TRUE(result.has_value());
    auto uri = result.value();

    EXPECT_EQ(uri.scheme().value(), "https");
    EXPECT_EQ(uri.host().value(), "example.com");
    EXPECT_EQ(uri.port().value(), 443);
    EXPECT_EQ(uri.path().value(), "/path/api");
}

TEST_CASE(TestUriBuilderAuthority)
{
    auto result = webcraft::web::core::uri_builder()
                      .scheme("http")
                      .authority("user:pass", "example.com", 8080)
                      .path("/test")
                      .build();

    EXPECT_TRUE(result.has_value());
    auto uri = result.value();

    EXPECT_EQ(uri.userinfo().value(), "user:pass");
    EXPECT_EQ(uri.host().value(), "example.com");
    EXPECT_EQ(uri.port().value(), 8080);
}

TEST_CASE(TestUriBuilderBuildString)
{
    auto builder = webcraft::web::core::uri_builder()
                       .scheme("https")
                       .host("example.com")
                       .port(443)
                       .path("/api")
                       .query("test=value")
                       .fragment("section");

    auto uri_string = builder.build_string();
    EXPECT_EQ(uri_string, "https://example.com:443/api?test=value#section");
}
