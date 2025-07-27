#define TEST_SUITE_NAME AsyncIOCoreTestSuite

#include "test_suite.hpp"
#include <webcraft/async/async.hpp>
#include <webcraft/async/io/core.hpp>
#include <string>
#include <vector>
#include <deque>
#include <span>

using namespace webcraft::async;
using namespace webcraft::async::io;
using namespace std::chrono_literals;

// Static assertions for concepts and type traits
static_assert(non_void_v<int>, "int should satisfy non_void_v");
static_assert(non_void_v<std::string>, "std::string should satisfy non_void_v");
static_assert(!non_void_v<void>, "void should not satisfy non_void_v");

// Static assertions for stream types
static_assert(std::is_abstract_v<async_readable_stream<int>>, "async_readable_stream should be abstract");
static_assert(std::is_abstract_v<async_writable_stream<int>>, "async_writable_stream should be abstract");

// Test value_type aliases
static_assert(std::same_as<async_readable_stream<int>::value_type, int>, "value_type should be int");
static_assert(std::same_as<async_writable_stream<std::string>::value_type, std::string>, "value_type should be std::string");

// Mock implementations for testing
template <typename T>
class mock_readable_stream : public async_readable_stream<T>
{
private:
    std::deque<T> data_;
    bool closed_ = false;

public:
    explicit mock_readable_stream(std::vector<T> data) : data_(data.begin(), data.end()) {}

    void close() { closed_ = true; }

    using async_readable_stream<T>::recv;

    task<std::optional<T>> recv() override
    {
        if (closed_ || data_.empty())
        {
            co_return std::nullopt;
        }

        auto value = std::move(data_.front());
        data_.pop_front();
        co_return value;
    }
};

template <typename T>
class mock_writable_stream : public async_writable_stream<T>
{
private:
    std::vector<T> received_;
    bool closed_ = false;

public:
    void close() { closed_ = true; }

    const std::vector<T> &received() const { return received_; }

    using async_writable_stream<T>::send;

    task<bool> send(T &&value) override
    {
        if (closed_)
        {
            co_return false;
        }
        received_.push_back(std::move(value));
        co_return true;
    }

    task<bool> send(const T &value) override
    {
        if (closed_)
        {
            co_return false;
        }
        received_.push_back(value);
        co_return true;
    }
};

TEST_CASE(BasicReadableStreamFunctionality)
{
    auto stream = std::make_unique<mock_readable_stream<int>>(std::vector<int>{1, 2, 3, 4, 5});

    auto task = [&]() -> webcraft::async::task<void>
    {
        // Test single recv
        auto value1 = co_await stream->recv();
        EXPECT_TRUE(value1.has_value());
        EXPECT_EQ(*value1, 1);

        auto value2 = co_await stream->recv();
        EXPECT_TRUE(value2.has_value());
        EXPECT_EQ(*value2, 2);

        // Test buffer recv
        std::array<int, 3> buffer;
        auto count = co_await stream->recv(std::span<int>(buffer));
        EXPECT_EQ(count, 3);
        EXPECT_EQ(buffer[0], 3);
        EXPECT_EQ(buffer[1], 4);
        EXPECT_EQ(buffer[2], 5);

        // Test empty stream
        auto empty_value = co_await stream->recv();
        EXPECT_FALSE(empty_value.has_value());
    };

    sync_wait(task());
}

TEST_CASE(ReadableStreamBufferRecv)
{
    auto stream = std::make_unique<mock_readable_stream<int>>(std::vector<int>{1, 2, 3});

    auto task = [&]() -> webcraft::async::task<void>
    {
        // Test with larger buffer than available data
        std::array<int, 5> buffer;
        auto count = co_await stream->recv(std::span<int>(buffer));
        EXPECT_EQ(count, 3);
        EXPECT_EQ(buffer[0], 1);
        EXPECT_EQ(buffer[1], 2);
        EXPECT_EQ(buffer[2], 3);

        // Test with empty buffer
        std::array<int, 0> empty_buffer;
        auto empty_count = co_await stream->recv(std::span<int>(empty_buffer));
        EXPECT_EQ(empty_count, 0);
    };

    sync_wait(task());
}

TEST_CASE(ReadableStreamToGenerator)
{
    auto stream = std::make_unique<mock_readable_stream<int>>(std::vector<int>{10, 20, 30});

    auto task = [&]() -> webcraft::async::task<void>
    {
        auto gen = stream->to_generator();
        std::vector<int> results;

        for (auto it = co_await gen.begin(); it != gen.end(); co_await ++it)
        {
            results.push_back(*it);
        }

        EXPECT_EQ(results.size(), 3);
        EXPECT_EQ(results[0], 10);
        EXPECT_EQ(results[1], 20);
        EXPECT_EQ(results[2], 30);
    };

    sync_wait(task());
}

TEST_CASE(ReadableStreamImplicitConversion)
{
    auto stream = std::make_unique<mock_readable_stream<int>>(std::vector<int>{100, 200});

    auto task = [&]() -> webcraft::async::task<void>
    {
        // Test implicit conversion to async_generator
        async_generator<int> gen = *stream;
        std::vector<int> results;

        for (auto it = co_await gen.begin(); it != gen.end(); co_await ++it)
        {
            results.push_back(*it);
        }

        EXPECT_EQ(results.size(), 2);
        EXPECT_EQ(results[0], 100);
        EXPECT_EQ(results[1], 200);
    };

    sync_wait(task());
}

TEST_CASE(BasicWritableStreamFunctionality)
{
    auto stream = std::make_unique<mock_writable_stream<std::string>>();

    auto task = [&]() -> webcraft::async::task<void>
    {
        // Test single send with rvalue
        bool sent1 = co_await stream->send(std::string("hello"));
        EXPECT_TRUE(sent1);

        // Test single send with lvalue
        std::string value = "world";
        bool sent2 = co_await stream->send(value);
        EXPECT_TRUE(sent2);

        EXPECT_EQ(stream->received().size(), 2);
        EXPECT_EQ(stream->received()[0], "hello");
        EXPECT_EQ(stream->received()[1], "world");
    };

    sync_wait(task());
}

TEST_CASE(WritableStreamBufferSend)
{
    auto stream = std::make_unique<mock_writable_stream<int>>();

    auto task = [&]() -> webcraft::async::task<void>
    {
        std::vector<int> data = {1, 2, 3, 4, 5};
        auto count = co_await stream->send(std::span<const int>(data));

        EXPECT_EQ(count, 5);
        EXPECT_EQ(stream->received().size(), 5);
        for (size_t i = 0; i < 5; ++i)
        {
            EXPECT_EQ(stream->received()[i], i + 1);
        }

        // Test with empty buffer
        std::vector<int> empty_data;
        auto empty_count = co_await stream->send(std::span<const int>(empty_data));
        EXPECT_EQ(empty_count, 0);
    };

    sync_wait(task());
}

TEST_CASE(WritableStreamPartialSend)
{
    auto stream = std::make_unique<mock_writable_stream<int>>();

    auto task = [&]() -> webcraft::async::task<void>
    {
        // Send some data first
        co_await stream->send(1);
        co_await stream->send(2);

        // Close the stream to simulate failure
        stream->close();

        // Try to send more data
        std::vector<int> data = {3, 4, 5};
        auto count = co_await stream->send(std::span<const int>(data));

        EXPECT_EQ(count, 0);                     // Should fail to send any
        EXPECT_EQ(stream->received().size(), 2); // Only the first two
    };

    sync_wait(task());
}

TEST_CASE(GeneratorToReadableStream)
{
    auto generator_func = []() -> async_generator<int>
    {
        co_yield 1;
        co_yield 2;
        co_yield 3;
    };

    auto stream = to_readable_stream(generator_func());

    auto task = [&]() -> webcraft::async::task<void>
    {
        std::vector<int> results;

        while (true)
        {
            auto value = co_await stream->recv();
            if (!value)
                break;
            results.push_back(*value);
        }

        EXPECT_EQ(results.size(), 3);
        EXPECT_EQ(results[0], 1);
        EXPECT_EQ(results[1], 2);
        EXPECT_EQ(results[2], 3);
    };

    sync_wait(task());
}

TEST_CASE(MPSCChannelBasicFunctionality)
{
    auto [reader, writer] = make_mpsc_channel<int>();

    auto task = [&]() -> webcraft::async::task<void>
    {
        // Test writing and reading
        bool sent1 = co_await writer->send(42);
        EXPECT_TRUE(sent1);

        bool sent2 = co_await writer->send(std::move(int(84)));
        EXPECT_TRUE(sent2);

        auto value1 = co_await reader->recv();
        EXPECT_TRUE(value1.has_value());
        EXPECT_EQ(*value1, 42);

        auto value2 = co_await reader->recv();
        EXPECT_TRUE(value2.has_value());
        EXPECT_EQ(*value2, 84);
    };

    sync_wait(task());
}

TEST_CASE(MPSCChannelMultipleValues)
{
    auto [reader, writer] = make_mpsc_channel<std::string>();

    auto task = [&]() -> webcraft::async::task<void>
    {
        // Send multiple values
        std::vector<std::string> test_data = {"hello", "world", "async", "io"};

        for (const auto &value : test_data)
        {
            bool sent = co_await writer->send(value);
            EXPECT_TRUE(sent);
        }

        // Read all values
        std::vector<std::string> received;
        for (size_t i = 0; i < test_data.size(); ++i)
        {
            auto value = co_await reader->recv();
            EXPECT_TRUE(value.has_value());
            received.push_back(*value);
        }

        EXPECT_EQ(received, test_data);
    };

    sync_wait(task());
}

TEST_CASE(MPSCChannelAsyncBehavior)
{
    auto [reader, writer] = make_mpsc_channel<int>();

    auto producer_task = [&]() -> webcraft::async::task<void>
    {
        for (int i = 0; i < 5; ++i)
        {
            bool sent = co_await writer->send(i);
            EXPECT_TRUE(sent);
        }
    };

    auto consumer_task = [&]() -> task<std::vector<int>>
    {
        std::vector<int> results;
        for (int i = 0; i < 5; ++i)
        {
            auto value = co_await reader->recv();
            EXPECT_TRUE(value.has_value());
            results.push_back(*value);
        }
        co_return results;
    };

    auto task = [&]() -> webcraft::async::task<void>
    {
        auto [_, results] = co_await when_all(producer_task(), consumer_task());

        EXPECT_EQ(results.size(), 5);
        for (int i = 0; i < 5; ++i)
        {
            EXPECT_EQ(results[i], i);
        }
    };

    sync_wait(task());
}

TEST_CASE(MPSCChannelTypeCompatibility)
{
    // Test with different types
    {
        auto [reader, writer] = make_mpsc_channel<std::vector<int>>();

        auto task = [&]() -> webcraft::async::task<void>
        {
            std::vector<int> test_vec = {1, 2, 3};
            bool sent = co_await writer->send(test_vec);
            EXPECT_TRUE(sent);

            auto received = co_await reader->recv();
            EXPECT_TRUE(received.has_value());
            EXPECT_EQ(*received, test_vec);
        };

        sync_wait(task());
    }

    // Test with custom struct
    struct TestStruct
    {
        int x, y;
        bool operator==(const TestStruct &other) const = default;
    };

    {
        auto [reader, writer] = make_mpsc_channel<TestStruct>();

        auto task = [&]() -> webcraft::async::task<void>
        {
            TestStruct test_data{10, 20};
            bool sent = co_await writer->send(test_data);
            EXPECT_TRUE(sent);

            auto received = co_await reader->recv();
            EXPECT_TRUE(received.has_value());
            EXPECT_EQ(*received, test_data);
        };

        sync_wait(task());
    }
}

TEST_CASE(ErrorHandling)
{
    // Test with closed stream
    auto stream = std::make_unique<mock_readable_stream<int>>(std::vector<int>{1, 2, 3});

    auto task = [&]() -> webcraft::async::task<void>
    {
        // Read one value
        auto value1 = co_await stream->recv();
        EXPECT_TRUE(value1.has_value());
        EXPECT_EQ(*value1, 1);

        // Close the stream
        stream->close();

        // Try to read more - should return nullopt
        auto value2 = co_await stream->recv();
        EXPECT_FALSE(value2.has_value());

        // Buffer read should return 0
        std::array<int, 5> buffer;
        auto count = co_await stream->recv(std::span<int>(buffer));
        EXPECT_EQ(count, 0);
    };

    sync_wait(task());
}

TEST_CASE(ConcurrentMPSCChannelAccess)
{
    auto [reader, writer] = make_mpsc_channel<uint64_t>();

    auto task = [&]() -> webcraft::async::task<void>
    {
        // Multiple producers
        auto producer1 = [&]() -> webcraft::async::task<void>
        {
            for (uint64_t i = 0; i < 100; ++i)
            {
                bool sent = co_await writer->send(i);
                EXPECT_TRUE(sent);
            }
        };

        auto producer2 = [&]() -> webcraft::async::task<void>
        {
            for (uint64_t i = 100; i < 200; ++i)
            {
                bool sent = co_await writer->send(i);
                EXPECT_TRUE(sent);
            }
        };

        auto consumer = [&]() -> webcraft::async::task<std::vector<uint64_t>>
        {
            std::vector<uint64_t> results;
            for (int i = 0; i < 200; ++i)
            {
                auto value = co_await reader->recv();
                EXPECT_TRUE(value.has_value());
                results.push_back(*value);
            }
            co_return results;
        };

        auto [_, __, results] = co_await when_all(producer1(), producer2(), consumer());

        EXPECT_EQ(results.size(), 200);

        // Check that we received all values (order might be different due to concurrency)
        std::sort(results.begin(), results.end());
        for (uint64_t i = 0; i < 200; ++i)
        {
            EXPECT_EQ(results[i], i);
        }
    };

    sync_wait(task());
}
