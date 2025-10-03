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