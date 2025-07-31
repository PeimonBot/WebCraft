#define TEST_SUITE_NAME AsyncIOAdaptorsTestSuite

#include "test_suite.hpp"
#include <webcraft/async/async.hpp>
#include <webcraft/async/io/core.hpp>
#include <webcraft/async/io/adaptors.hpp>
#include <string>
#include <vector>
#include <deque>
#include <span>
#include <functional>
#include <algorithm>
#include <numeric>

using namespace webcraft::async;
using namespace webcraft::async::io;
using namespace webcraft::async::io::adaptors;
using namespace std::chrono_literals;

template <typename T>
class mock_readable_stream
{
public:
    explicit mock_readable_stream(std::vector<T> values) : values(std::move(values)) {}

    task<std::optional<T>> recv()
    {
        if (values.empty())
        {
            co_return std::nullopt;
        }
        T value = std::move(values.front());
        values.erase(values.begin());
        co_return std::make_optional(std::move(value));
    }

private:
    std::vector<T> values;
};

template <typename T>
class mock_writable_stream
{
private:
    std::queue<T> received_values;

public:
    task<bool> send(T &&value)
    {
        received_values.push(std::move(value));
        co_return true;
    }

    bool received(T value)
    {
        auto received = received_values.front();
        received_values.pop();
        return received == value;
    }
};

static_assert(async_readable_stream<mock_readable_stream<int>, int>, "mock_readable_stream should be an async readable stream");
static_assert(async_writable_stream<mock_writable_stream<int>, int>, "mock_writable_stream should be an async writable stream");
static_assert(async_readable_stream<mock_readable_stream<std::string>, std::string>, "mock_readable_stream should be an async readable stream");
static_assert(async_writable_stream<mock_writable_stream<std::string>, std::string>, "mock_writable_stream should be an async writable stream");

TEST_CASE(TestTransformStreamAdaptor)
{
    std::vector<int> values({1, 2, 3, 4, 5});
    mock_readable_stream<int> stream(values);

    auto transform_fn = [](async_generator<int> gen) -> async_generator<int>
    {
        for_each_async(value, gen,
                       {
                           co_yield value * 2 - 1; // Transform by multiplying each value by 2
                           co_yield value * 2;     // Transform by multiplying each value by 2
                       });
    };

    auto task_fn = [&]() -> task<void>
    {
        auto transformed_stream = stream | transform<int>(std::move(transform_fn));
        std::vector<int> results;

        while (auto value = co_await transformed_stream.recv())
        {
            results.push_back(std::move(*value));
        }

        EXPECT_EQ(results.size(), 10) << "Should read five values from the transformed stream";
        size_t counter = 0;
        for (size_t val : values)
        {
            auto check = val * 2 - 1;
            EXPECT_EQ(check, results[counter]) << "Expected: " << check << ", Actual: " << results[counter];
            counter++;
            check = val * 2;
            EXPECT_EQ(check, results[counter]) << "Expected: " << check << ", Actual: " << results[counter];
            counter++;
        }
    };

    sync_wait(task_fn());
}

TEST_CASE(TestTransformStreamAdaptorReturningString)
{
    mock_readable_stream<int> stream({1, 2, 3, 4, 5});

    auto transform_fn = [](async_generator<int> gen) -> async_generator<std::string>
    {
        for_each_async(value, gen,
                       {
                           co_yield std::to_string(value * 2); // Transform by multiplying each value by 2 and converting to string
                       });
    };

    auto task_fn = [&]() -> task<void>
    {
        auto transformed_stream = stream | transform<int>(std::move(transform_fn));
        std::vector<std::string> results;

        while (auto value = co_await transformed_stream.recv())
        {
            results.push_back(std::move(*value));
        }

        EXPECT_EQ(results.size(), 5) << "Should read five values from the transformed stream";
        for (size_t i = 0; i < results.size(); ++i)
        {
            EXPECT_EQ(results[i], std::to_string((i + 1) * 2)) << "Value at index " << i << " should be " << (i + 1) * 2;
        }
    };
}

TEST_CASE(TestMapStreamAdaptor)
{
    mock_readable_stream<int> stream({1, 2, 3, 4, 5});

    auto map_fn = [](int value) -> std::string
    {
        return "Value: " + std::to_string(value * 2); // Map by multiplying each value by 2 and converting to string
    };

    auto task_fn = [&]() -> task<void>
    {
        auto mapped_stream = stream | map<int, std::string>(std::move(map_fn));
        std::vector<std::string> results;

        while (auto value = co_await mapped_stream.recv())
        {
            results.push_back(std::move(*value));
        }

        EXPECT_EQ(results.size(), 5) << "Should read five values from the mapped stream";
        for (size_t i = 0; i < results.size(); ++i)
        {
            EXPECT_EQ(results[i], "Value: " + std::to_string((i + 1) * 2)) << "Value at index " << i << " should be 'Value: " << (i + 1) * 2 << "'";
        }
    };

    sync_wait(task_fn());
}