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
    mock_readable_stream<int> stream({1, 2, 3, 4, 5});

    auto transform_fn = [](async_generator<int> gen) -> async_generator<int>
    {
        for (auto it = co_await gen.begin(); it != gen.end(); co_await ++it)
        {
            co_yield *it * 2; // Transform by multiplying each value by 2
        }
    };

    auto task_fn = [&]() -> task<void>
    {
        auto transformed_stream = stream | transform<int>(std::move(transform_fn));
        std::vector<int> results;

        while (auto value = co_await transformed_stream.recv())
        {
            results.push_back(std::move(*value));
        }

        EXPECT_EQ(results.size(), 5) << "Should read five values from the transformed stream";
        for (size_t i = 0; i < results.size(); ++i)
        {
            EXPECT_EQ(results[i], (i + 1) * 2) << "Value at index " << i << " should be " << (i + 1) * 2;
        }
    };

    sync_wait(task_fn());
}