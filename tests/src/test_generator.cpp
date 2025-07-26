///////////////////////////////////////////////////////////////////////////////
// Copyright (c) Lewis Baker
// Licenced under MIT license. See LICENSE.txt for details.
///////////////////////////////////////////////////////////////////////////////

#define TEST_SUITE_NAME GeneratorTestSuite

#include "test_suite.hpp"

#include <webcraft/async/generator.hpp>
#include <webcraft/async/async_generator.hpp>

using namespace webcraft::ranges;
using namespace webcraft::async;

TEST_CASE(EmptyGeneratorTest)
{
    generator<int> ints;
    EXPECT_TRUE(ints.begin() == ints.end());
}

TEST_CASE(ReturnsCopyTest)
{
    auto f = []() -> generator<float>
    {
        co_yield 1.0f;
        co_yield 2.0f;
    };

    auto gen = f();
    auto iter = gen.begin();
    // TODO: Should this really be required?
    // static_assert(std::is_same<decltype(*iter), float>::value, "operator* should return float by value");
    EXPECT_TRUE(*iter == 1.0f);
    ++iter;
    EXPECT_TRUE(*iter == 2.0f);
    ++iter;
    EXPECT_TRUE(iter == gen.end());
}

TEST_CASE(ReturnsReferenceTest)
{
    auto f = [](float &value) -> generator<float &>
    {
        co_yield value;
    };

    float value = 1.0f;
    for (auto &x : f(value))
    {
        EXPECT_TRUE(&x == &value);
        x += 1.0f;
    }

    EXPECT_TRUE(value == 2.0f);
}

TEST_CASE(ConstTypeTest)
{
    auto fib = []() -> generator<const std::uint64_t>
    {
        std::uint64_t a = 0, b = 1;
        while (true)
        {
            co_yield b;
            b += std::exchange(a, b);
        }
    };

    std::uint64_t count = 0;
    for (auto i : fib())
    {
        if (i > 1'000'000)
        {
            break;
        }
        ++count;
    }

    // 30th fib number is 832'040
    EXPECT_TRUE(count == 30);
}

TEST_CASE(LazynessTest)
{
    bool reachedA = false;
    bool reachedB = false;
    bool reachedC = false;
    auto f = [&]() -> generator<int>
    {
        reachedA = true;
        co_yield 1;
        reachedB = true;
        co_yield 2;
        reachedC = true;
    };

    auto gen = f();
    EXPECT_TRUE(!reachedA);
    auto iter = gen.begin();
    EXPECT_TRUE(reachedA);
    EXPECT_TRUE(!reachedB);
    EXPECT_TRUE(*iter == 1);
    ++iter;
    EXPECT_TRUE(reachedB);
    EXPECT_TRUE(!reachedC);
    EXPECT_TRUE(*iter == 2);
    ++iter;
    EXPECT_TRUE(reachedC);
    EXPECT_TRUE(iter == gen.end());
}

TEST_CASE(ThrowingBeforeYieldThrowsOnBegin)
{
    class X
    {
    };

    auto g = []() -> webcraft::ranges::generator<int>
    {
        throw X{};
        co_return;
    }();

    EXPECT_THROW(g.begin(), X) << "should throw X when trying to advance past the first element";
}

TEST_CASE(ThrowingAfterYieldThrowsOnNext)
{
    class X
    {
    };

    auto g = []() -> webcraft::ranges::generator<int>
    {
        co_yield 1;
        throw X{};
    }();

    auto iter = g.begin();
    EXPECT_TRUE(iter != g.end());
    EXPECT_THROW(++iter, X) << "should throw X when trying to advance past the first element";
}

namespace
{
    template <typename FIRST, typename SECOND>
    auto concat(FIRST &&first, SECOND &&second)
    {
        using value_type = std::remove_reference_t<decltype(*first.begin())>;
        return [](FIRST first, SECOND second) -> webcraft::ranges::generator<value_type>
        {
            for (auto &&x : first)
                co_yield x;
            for (auto &&y : second)
                co_yield y;
        }(std::forward<FIRST>(first), std::forward<SECOND>(second));
    }
}

TEST_CASE(SafeCaptureOfRValueReferenceArgs)
{
    using namespace std::string_literals;

    // EXPECT_TRUE that we can capture l-values by reference and that temporary
    // values are moved into the coroutine frame.
    std::string byRef = "bar";
    auto g = concat("foo"s, concat(byRef, std::vector<char>{'b', 'a', 'z'}));

    byRef = "buzz";

    std::string s;
    for (char c : g)
    {
        s += c;
    }

    EXPECT_TRUE(s == "foobuzzbaz");
}

namespace
{
    webcraft::ranges::generator<int> range(int start, int end)
    {
        for (; start < end; ++start)
        {
            co_yield start;
        }
    }
}

namespace
{
    template <std::size_t window, typename Range>
    webcraft::ranges::generator<const double> low_pass(Range rng)
    {
        auto it = std::begin(rng);
        const auto itEnd = std::end(rng);

        const double invCount = 1.0 / window;
        double sum = 0;

        using iter_cat =
            typename std::iterator_traits<decltype(it)>::iterator_category;

        if constexpr (std::is_base_of_v<std::random_access_iterator_tag, iter_cat>)
        {
            for (std::size_t count = 0; it != itEnd && count < window; ++it)
            {
                sum += *it;
                ++count;
                co_yield sum / count;
            }

            for (; it != itEnd; ++it)
            {
                sum -= *(it - window);
                sum += *it;
                co_yield sum *invCount;
            }
        }
        else if constexpr (std::is_base_of_v<std::forward_iterator_tag, iter_cat>)
        {
            auto windowStart = it;
            for (std::size_t count = 0; it != itEnd && count < window; ++it)
            {
                sum += *it;
                ++count;
                co_yield sum / count;
            }

            for (; it != itEnd; ++it, ++windowStart)
            {
                sum -= *windowStart;
                sum += *it;
                co_yield sum *invCount;
            }
        }
        else
        {
            // Just assume an input iterator
            double buffer[window];

            for (std::size_t count = 0; it != itEnd && count < window; ++it)
            {
                buffer[count] = *it;
                sum += buffer[count];
                ++count;
                co_yield sum / count;
            }

            for (std::size_t pos = 0; it != itEnd; ++it, pos = (pos + 1 == window) ? 0 : (pos + 1))
            {
                sum -= std::exchange(buffer[pos], *it);
                sum += buffer[pos];
                co_yield sum *invCount;
            }
        }
    }
}

TEST_CASE(TestRangesInterop)
{
    auto gen_fn = []() -> webcraft::ranges::generator<int>
    {
        for (int i = 0; i < 10; ++i)
        {
            co_yield i;
        }
    };

    auto range = gen_fn() | std::views::transform([](int i)
                                                  { return i * 2; }) |
                 std::views::drop_while([](int i)
                                        { return i < 10; });

    for (auto &&value : range)
    {
        EXPECT_TRUE(value % 2 == 0) << "Value should be even";
        EXPECT_TRUE(value >= 10) << "Value should be greater than or equal to 10";
    }
}