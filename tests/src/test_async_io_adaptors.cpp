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
// Mock implementations for testing

template <typename T>
class test_readable_stream : public async_readable_stream<T>
{
private:
    std::deque<T> data_;

public:
    explicit test_readable_stream(std::vector<T> data) : data_(data.begin(), data.end()) {}

    task<std::optional<T>> recv() override
    {
        if (data_.empty())
        {
            co_return std::nullopt;
        }

        auto value = std::move(data_.front());
        data_.pop_front();
        co_return value;
    }
};

template <typename T>
class test_writable_stream : public async_writable_stream<T>
{
private:
    std::vector<T> received_;

public:
    const std::vector<T> &received() const { return received_; }

    task<bool> send(T &&value) override
    {
        received_.push_back(std::move(value));
        co_return true;
    }

    task<bool> send(const T &value) override
    {
        received_.push_back(value);
        co_return true;
    }
};

static_assert(std::is_base_of_v<async_readable_stream<int>, test_readable_stream<int>>, "test_readable_stream should inherit from async_readable_stream");
static_assert(std::is_base_of_v<async_writable_stream<std::string>, test_writable_stream<std::string>>, "test_writable_stream should inherit from async_writable_stream");

// Helper function to create readable stream
template <typename T>
std::unique_ptr<async_readable_stream<T>> make_test_stream(std::vector<T> data)
{
    return std::make_unique<test_readable_stream<T>>(std::move(data));
}

TEST_CASE(PipeAdaptor)
{
    auto sink = std::make_shared<test_writable_stream<int>>();
    auto source = make_test_stream<int>({1, 2, 3, 4, 5});

    auto task = [&]() -> webcraft::async::task<void>
    {
        auto piped_stream = std::move(source) | webcraft::async::io::adaptors::pipe(static_cast<std::shared_ptr<async_writable_stream<int>>>(sink));

        std::vector<int> results;
        while (true)
        {
            auto value = co_await piped_stream->recv();
            if (!value)
                break;
            results.push_back(*value);
        }

        // Check that all values were received
        EXPECT_EQ(results.size(), 5);
        for (int i = 0; i < 5; ++i)
        {
            EXPECT_EQ(results[i], i + 1);
        }

        // Check that all values were piped to sink
        EXPECT_EQ(sink->received().size(), 5);
        for (int i = 0; i < 5; ++i)
        {
            EXPECT_EQ(sink->received()[i], i + 1);
        }
    };

    sync_wait(task());
}

TEST_CASE(MapAdaptor)
{
    auto source = make_test_stream<int>({1, 2, 3, 4, 5});

    auto task = [&]() -> webcraft::async::task<void>
    {
        // Map int to string
        auto mapped_stream = std::move(source) | map<int>([](int x)
                                                          { return std::to_string(x * 2); });

        std::vector<std::string> results;
        while (true)
        {
            auto value = co_await mapped_stream->recv();
            if (!value)
                break;
            results.push_back(*value);
        }

        EXPECT_EQ(results.size(), 5);
        EXPECT_EQ(results[0], "2");
        EXPECT_EQ(results[1], "4");
        EXPECT_EQ(results[2], "6");
        EXPECT_EQ(results[3], "8");
        EXPECT_EQ(results[4], "10");
    };

    sync_wait(task());
}

TEST_CASE(ForEachAdaptor)
{
    auto source = make_test_stream<int>({10, 20, 30});

    auto task = [&]() -> webcraft::async::task<void>
    {
        std::vector<int> collected;

        co_await (std::move(source) | for_each<int>([&](const int &value)
                                                    { collected.push_back(value); }));

        EXPECT_EQ(collected.size(), 3);
        EXPECT_EQ(collected[0], 10);
        EXPECT_EQ(collected[1], 20);
        EXPECT_EQ(collected[2], 30);
    };

    sync_wait(task());
}

TEST_CASE(TransformAdaptor)
{
    auto source = make_test_stream<int>({1, 2, 3});

    auto task = [&]() -> webcraft::async::task<void>
    {
        auto transform_func = [](async_generator<int> gen) -> async_generator<std::string>
        {
            auto it = co_await gen.begin();
            while (it != gen.end())
            {
                co_yield "value_" + std::to_string(*it);
                co_yield "extra_" + std::to_string(*it);
                co_await ++it;
            }
        };

        auto transformed_stream = std::move(source) | transform<int, std::string>(transform_func);

        std::vector<std::string> results;
        while (true)
        {
            auto value = co_await transformed_stream->recv();
            if (!value)
                break;
            results.push_back(*value);
        }

        EXPECT_EQ(results.size(), 6);
        EXPECT_EQ(results[0], "value_1");
        EXPECT_EQ(results[1], "extra_1");
        EXPECT_EQ(results[2], "value_2");
        EXPECT_EQ(results[3], "extra_2");
        EXPECT_EQ(results[4], "value_3");
        EXPECT_EQ(results[5], "extra_3");
    };

    sync_wait(task());
}

TEST_CASE(FilterAdaptor)
{
    auto source = make_test_stream<int>({1, 2, 3, 4, 5, 6, 7, 8, 9, 10});

    auto task = [&]() -> webcraft::async::task<void>
    {
        // Filter even numbers
        auto filtered_stream = std::move(source) | filter<int>([](const int &x)
                                                               { return x % 2 == 0; });

        std::vector<int> results;
        while (true)
        {
            auto value = co_await filtered_stream->recv();
            if (!value)
                break;
            results.push_back(*value);
        }

        EXPECT_EQ(results.size(), 5);
        EXPECT_EQ(results[0], 2);
        EXPECT_EQ(results[1], 4);
        EXPECT_EQ(results[2], 6);
        EXPECT_EQ(results[3], 8);
        EXPECT_EQ(results[4], 10);
    };

    sync_wait(task());
}

TEST_CASE(TakeWhileAdaptor)
{
    auto source = make_test_stream<int>({1, 2, 3, 4, 5, 1, 2});

    auto task = [&]() -> webcraft::async::task<void>
    {
        // Take while less than 5
        auto taken_stream = std::move(source) | take_while<int>([](const int &x)
                                                                { return x < 5; });

        std::vector<int> results;
        while (true)
        {
            auto value = co_await taken_stream->recv();
            if (!value)
                break;
            results.push_back(*value);
        }

        EXPECT_EQ(results.size(), 4);
        EXPECT_EQ(results[0], 1);
        EXPECT_EQ(results[1], 2);
        EXPECT_EQ(results[2], 3);
        EXPECT_EQ(results[3], 4);
    };

    sync_wait(task());
}

TEST_CASE(DropWhileAdaptor)
{
    auto source = make_test_stream<int>({1, 2, 3, 6, 7, 8});

    auto task = [&]() -> webcraft::async::task<void>
    {
        // Drop while less than 5
        auto dropped_stream = std::move(source) | drop_while<int>([](const int &x)
                                                                  { return x < 5; });

        std::vector<int> results;
        while (true)
        {
            auto value = co_await dropped_stream->recv();
            if (!value)
                break;
            results.push_back(*value);
        }

        EXPECT_EQ(results.size(), 3);
        EXPECT_EQ(results[0], 6);
        EXPECT_EQ(results[1], 7);
        EXPECT_EQ(results[2], 8);
    };

    sync_wait(task());
}

TEST_CASE(TakeAdaptor)
{
    auto source = make_test_stream<int>({1, 2, 3, 4, 5, 6, 7, 8, 9, 10});

    auto task = [&]() -> webcraft::async::task<void>
    {
        // Take first 3 elements
        auto taken_stream = std::move(source) | take<int>(3);

        std::vector<int> results;
        while (true)
        {
            auto value = co_await taken_stream->recv();
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

TEST_CASE(DropAdaptor)
{
    auto source = make_test_stream<int>({1, 2, 3, 4, 5});

    auto task = [&]() -> webcraft::async::task<void>
    {
        // Drop first 2 elements
        auto dropped_stream = std::move(source) | drop<int>(2);

        std::vector<int> results;
        while (true)
        {
            auto value = co_await dropped_stream->recv();
            if (!value)
                break;
            results.push_back(*value);
        }

        EXPECT_EQ(results.size(), 3);
        EXPECT_EQ(results[0], 3);
        EXPECT_EQ(results[1], 4);
        EXPECT_EQ(results[2], 5);
    };

    sync_wait(task());
}

TEST_CASE(EnumerateAdaptor)
{
    auto source = make_test_stream<std::string>({"hello", "world", "async"});

    auto task = [&]() -> webcraft::async::task<void>
    {
        auto enumerated_stream = std::move(source) | enumerate<std::string>();

        std::vector<std::pair<size_t, std::string>> results;
        while (true)
        {
            auto value = co_await enumerated_stream->recv();
            if (!value)
                break;
            results.push_back(*value);
        }

        EXPECT_EQ(results.size(), 3);
        EXPECT_EQ(results[0].first, 0);
        EXPECT_EQ(results[0].second, "hello");
        EXPECT_EQ(results[1].first, 1);
        EXPECT_EQ(results[1].second, "world");
        EXPECT_EQ(results[2].first, 2);
        EXPECT_EQ(results[2].second, "async");
    };

    sync_wait(task());
}

TEST_CASE(ChunkAdaptor)
{
    auto source = make_test_stream<int>({1, 2, 3, 4, 5, 6, 7, 8, 9});

    auto task = [&]() -> webcraft::async::task<void>
    {
        auto chunked_stream = std::move(source) | chunk<int>(3);

        std::vector<std::vector<int>> results;
        while (true)
        {
            auto value = co_await chunked_stream->recv();
            if (!value)
                break;
            results.push_back(*value);
        }

        EXPECT_EQ(results.size(), 3);
        EXPECT_EQ(results[0], std::vector<int>({1, 2, 3}));
        EXPECT_EQ(results[1], std::vector<int>({4, 5, 6}));
        EXPECT_EQ(results[2], std::vector<int>({7, 8, 9}));
    };

    sync_wait(task());
}

TEST_CASE(ChunkAdaptorPartialChunk)
{
    auto source = make_test_stream<int>({1, 2, 3, 4, 5});

    auto task = [&]() -> webcraft::async::task<void>
    {
        auto chunked_stream = std::move(source) | chunk<int>(3);

        std::vector<std::vector<int>> results;
        while (true)
        {
            auto value = co_await chunked_stream->recv();
            if (!value)
                break;
            results.push_back(*value);
        }

        EXPECT_EQ(results.size(), 2);
        EXPECT_EQ(results[0], std::vector<int>({1, 2, 3}));
        EXPECT_EQ(results[1], std::vector<int>({4, 5}));
    };

    sync_wait(task());
}

TEST_CASE(CountCollector)
{
    auto source = make_test_stream<int>({10, 20, 30, 40, 50});

    auto task = [&]() -> webcraft::async::task<void>
    {
        auto gen = source->to_generator();
        auto count_result = co_await count<int>()(std::move(gen));

        EXPECT_EQ(count_result, 5);
    };

    sync_wait(task());
}

TEST_CASE(ZipAdaptorInner)
{
    auto source1 = make_test_stream<int>({1, 2, 3});
    auto source2 = make_test_stream<int>({10, 20, 30, 40});

    auto task = [&]() -> webcraft::async::task<void>
    {
        auto zipped_stream = std::move(source1) | zip(std::move(source2), zip_strategy::INNER);

        std::vector<std::pair<std::optional<int>, std::optional<int>>> results;
        while (true)
        {
            auto value = co_await zipped_stream->recv();
            if (!value)
                break;
            results.push_back(*value);
        }

        EXPECT_EQ(results.size(), 3);
        EXPECT_EQ(results[0].first, 1);
        EXPECT_EQ(results[0].second, 10);
        EXPECT_EQ(results[1].first, 2);
        EXPECT_EQ(results[1].second, 20);
        EXPECT_EQ(results[2].first, 3);
        EXPECT_EQ(results[2].second, 30);
    };

    sync_wait(task());
}

TEST_CASE(ZipAdaptorFull)
{
    auto source1 = make_test_stream<int>({1, 2});
    auto source2 = make_test_stream<int>({10, 20, 30});

    auto task = [&]() -> webcraft::async::task<void>
    {
        auto zipped_stream = std::move(source1) | zip(std::move(source2), zip_strategy::FULL);

        std::vector<std::pair<std::optional<int>, std::optional<int>>> results;
        while (true)
        {
            auto value = co_await zipped_stream->recv();
            if (!value)
                break;
            results.push_back(*value);
        }

        EXPECT_EQ(results.size(), 3);
        EXPECT_EQ(results[0].first, 1);
        EXPECT_EQ(results[0].second, 10);
        EXPECT_EQ(results[1].first, 2);
        EXPECT_EQ(results[1].second, 20);
        EXPECT_EQ(results[2].first, std::nullopt);
        EXPECT_EQ(results[2].second, 30);
    };

    sync_wait(task());
}

TEST_CASE(ZipAdaptorLeft)
{
    auto source1 = make_test_stream<int>({1, 2, 3});
    auto source2 = make_test_stream<int>({10, 20});

    auto task = [&]() -> webcraft::async::task<void>
    {
        auto zipped_stream = std::move(source1) | zip(std::move(source2), zip_strategy::LEFT);

        std::vector<std::pair<std::optional<int>, std::optional<int>>> results;
        while (true)
        {
            auto value = co_await zipped_stream->recv();
            if (!value)
                break;
            results.push_back(*value);
        }

        EXPECT_EQ(results.size(), 3);
        EXPECT_EQ(results[0].first, 1);
        EXPECT_EQ(results[0].second, 10);
        EXPECT_EQ(results[1].first, 2);
        EXPECT_EQ(results[1].second, 20);
        EXPECT_EQ(results[2].first, 3);
        EXPECT_EQ(results[2].second, std::nullopt);
    };

    sync_wait(task());
}

TEST_CASE(ZipAdaptorRight)
{
    auto source1 = make_test_stream<int>({1, 2});
    auto source2 = make_test_stream<int>({10, 20, 30});

    auto task = [&]() -> webcraft::async::task<void>
    {
        auto zipped_stream = std::move(source1) | zip(std::move(source2), zip_strategy::RIGHT);

        std::vector<std::pair<std::optional<int>, std::optional<int>>> results;
        while (true)
        {
            auto value = co_await zipped_stream->recv();
            if (!value)
                break;
            results.push_back(*value);
        }

        EXPECT_EQ(results.size(), 3);
        EXPECT_EQ(results[0].first, 1);
        EXPECT_EQ(results[0].second, 10);
        EXPECT_EQ(results[1].first, 2);
        EXPECT_EQ(results[1].second, 20);
        EXPECT_EQ(results[2].first, std::nullopt);
        EXPECT_EQ(results[2].second, 30);
    };

    sync_wait(task());
}

TEST_CASE(FlattenAdaptor)
{
    auto source = make_test_stream<std::vector<int>>({{1, 2}, {3, 4, 5}, {6}});

    auto task = [&]() -> webcraft::async::task<void>
    {
        auto flattened_stream = std::move(source) | flatten<std::vector<int>>();

        std::vector<int> results;
        while (true)
        {
            auto value = co_await flattened_stream->recv();
            if (!value)
                break;
            results.push_back(*value);
        }

        EXPECT_EQ(results.size(), 6);
        EXPECT_EQ(results[0], 1);
        EXPECT_EQ(results[1], 2);
        EXPECT_EQ(results[2], 3);
        EXPECT_EQ(results[3], 4);
        EXPECT_EQ(results[4], 5);
        EXPECT_EQ(results[5], 6);
    };

    sync_wait(task());
}

TEST_CASE(ToAdaptor)
{
    auto source = make_test_stream<int>({1, 2, 3, 4, 5});
    auto sink = std::make_shared<test_writable_stream<int>>();
    std::shared_ptr<async_writable_stream<int>> sink_ptr = sink;

    auto task = [&]() -> webcraft::async::task<void>
    {
        co_await (std::move(source) | to(sink_ptr));

        EXPECT_EQ(sink->received().size(), 5);
        for (int i = 0; i < 5; ++i)
        {
            EXPECT_EQ(sink->received()[i], i + 1);
        }
    };

    sync_wait(task());
}

TEST_CASE(ChainedAdaptors)
{
    auto source = make_test_stream<int>({1, 2, 3, 4, 5, 6, 7, 8, 9, 10});

    auto task = [&]() -> webcraft::async::task<void>
    {
        // Chain multiple adaptors: filter even numbers, map to string, take first 3
        auto processed_stream = std::move(source) | filter<int>([](const int &x)
                                                                { return x % 2 == 0; }) |
                                map<int>([](int x)
                                         { return "num_" + std::to_string(x); }) |
                                take<std::string>(3);

        std::vector<std::string> results;
        while (true)
        {
            auto value = co_await processed_stream->recv();
            if (!value)
                break;
            results.push_back(*value);
        }

        EXPECT_EQ(results.size(), 3);
        EXPECT_EQ(results[0], "num_2");
        EXPECT_EQ(results[1], "num_4");
        EXPECT_EQ(results[2], "num_6");
    };

    sync_wait(task());
}

TEST_CASE(ComplexChainedAdaptors)
{
    auto source = make_test_stream<int>({1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12});

    auto task = [&]() -> webcraft::async::task<void>
    {
        // Complex chain: drop first 2, take 8, chunk by 3, enumerate, filter by index
        auto processed_stream = std::move(source) | drop<int>(2) // {3, 4, 5, 6, 7, 8, 9, 10, 11, 12}
                                | take<int>(8)                   // {3, 4, 5, 6, 7, 8, 9, 10}
                                | chunk<int>(3)                  // {{3, 4, 5}, {6, 7, 8}, {9, 10}}
                                | enumerate<std::vector<int>>()  // {(0, {3, 4, 5}), (1, {6, 7, 8}), (2, {9, 10})}
                                | filter<std::pair<size_t, std::vector<int>>>([](const auto &p)
                                                                              { return p.first % 2 == 0; }); // {(0, {3, 4, 5}), (2, {9, 10})}

        std::vector<std::pair<size_t, std::vector<int>>> results;
        while (true)
        {
            auto value = co_await processed_stream->recv();
            if (!value)
                break;
            results.push_back(*value);
        }

        EXPECT_EQ(results.size(), 2);
        EXPECT_EQ(results[0].first, 0);
        EXPECT_EQ(results[0].second, std::vector<int>({3, 4, 5}));
        EXPECT_EQ(results[1].first, 2);
        EXPECT_EQ(results[1].second, std::vector<int>({9, 10}));
    };

    sync_wait(task());
}

TEST_CASE(ZipStrategyHelpers)
{
    EXPECT_TRUE(allows_left_zip(zip_strategy::FULL));
    EXPECT_TRUE(allows_left_zip(zip_strategy::LEFT));
    EXPECT_FALSE(allows_left_zip(zip_strategy::RIGHT));
    EXPECT_FALSE(allows_left_zip(zip_strategy::INNER));

    EXPECT_TRUE(allows_right_zip(zip_strategy::FULL));
    EXPECT_FALSE(allows_right_zip(zip_strategy::LEFT));
    EXPECT_TRUE(allows_right_zip(zip_strategy::RIGHT));
    EXPECT_FALSE(allows_right_zip(zip_strategy::INNER));
}

TEST_CASE(EmptyStreamHandling)
{
    auto empty_source = make_test_stream<int>({});

    auto task = [&]() -> webcraft::async::task<void>
    {
        // Test various adaptors with empty stream
        auto processed_stream = std::move(empty_source) | filter<int>([](const int &x)
                                                                      { return true; }) |
                                map<int>([](int x)
                                         { return x * 2; }) |
                                take<int>(5);

        std::vector<int> results;
        while (true)
        {
            auto value = co_await processed_stream->recv();
            if (!value)
                break;
            results.push_back(*value);
        }

        EXPECT_EQ(results.size(), 0);
    };

    sync_wait(task());
}

TEST_CASE(EdgeCaseBehaviors)
{
    // Test take(0)
    {
        auto source = make_test_stream<int>({1, 2, 3});

        auto task = [&]() -> webcraft::async::task<void>
        {
            auto taken_stream = std::move(source) | take<int>(0);

            auto value = co_await taken_stream->recv();
            EXPECT_FALSE(value.has_value());
        };

        sync_wait(task());
    }

    // Test drop with count larger than stream size
    {
        auto source = make_test_stream<int>({1, 2, 3});

        auto task = [&]() -> webcraft::async::task<void>
        {
            auto dropped_stream = std::move(source) | drop<int>(10);

            auto value = co_await dropped_stream->recv();
            EXPECT_FALSE(value.has_value());
        };

        sync_wait(task());
    }

    // Test chunk with size 1
    {
        auto source = make_test_stream<int>({1, 2, 3});

        auto task = [&]() -> webcraft::async::task<void>
        {
            auto chunked_stream = std::move(source) | chunk<int>(1);

            std::vector<std::vector<int>> results;
            while (true)
            {
                auto value = co_await chunked_stream->recv();
                if (!value)
                    break;
                results.push_back(*value);
            }

            EXPECT_EQ(results.size(), 3);
            EXPECT_EQ(results[0], std::vector<int>({1}));
            EXPECT_EQ(results[1], std::vector<int>({2}));
            EXPECT_EQ(results[2], std::vector<int>({3}));
        };

        sync_wait(task());
    }
}
