///////////////////////////////////////////////////////////////////////////////
// Copyright (c) Aditya Rao
// Licenced under MIT license. See LICENSE.txt for details.
///////////////////////////////////////////////////////////////////////////////

#define TEST_SUITE_NAME TaskTestSuite

#include "test_suite.hpp"
#include <webcraft/ranges.hpp>
#include <string>
#include <set>
#include <list>
#include <vector>

using namespace webcraft::ranges;

TEST_CASE(TestRangesDummyList)
{
    std::vector<int> vec = {1, 2, 3, 4, 5};

    auto range = vec | std::views::transform([](int x)
                                             { return x * 2; }) |
                 to<std::vector<int>>();

    static_assert(std::same_as<decltype(range), std::vector<int>>, "Range should be converted to std::vector<int>");
    EXPECT_EQ(range, (std::vector<int>{2, 4, 6, 8, 10})) << "Range should contain transformed values";
}

TEST_CASE(TestRangesWithSet)
{
    std::set<int> s = {1, 2, 3, 4, 5};

    auto range = s | std::views::transform([](int x)
                                           { return x * 2; }) |
                 to<std::set<int>>();

    static_assert(std::same_as<decltype(range), std::set<int>>, "Range should be converted to std::set<int>");
    EXPECT_EQ(range, (std::set<int>{2, 4, 6, 8, 10})) << "Range should contain transformed values";
}

TEST_CASE(TestRangesWithString)
{
    std::string str = "hello";

    auto range = str | std::views::transform([](char c)
                                             { return static_cast<char>(c - 32); }) |
                 to<std::string>();

    static_assert(std::same_as<decltype(range), std::string>, "Range should be converted to std::string");
    EXPECT_EQ(range, "HELLO") << "Range should contain transformed characters";
}

TEST_CASE(TestWithDropWhile)
{
    std::vector<int> vec = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

    auto range = vec | std::views::drop_while([](int x)
                                              { return x < 5; }) |
                 to<std::vector<int>>();

    static_assert(std::same_as<decltype(range), std::vector<int>>, "Range should be converted to std::vector<int>");
    EXPECT_EQ(range, (std::vector<int>{5, 6, 7, 8, 9, 10})) << "Range should contain elements after dropping while condition";
}

TEST_CASE(TestWithTakeWhile)
{
    std::vector<int> vec = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

    auto range = vec | std::views::take_while([](int x)
                                              { return x < 5; }) |
                 to<std::vector<int>>();

    static_assert(std::same_as<decltype(range), std::vector<int>>, "Range should be converted to std::vector<int>");
    EXPECT_EQ(range, (std::vector<int>{1, 2, 3, 4})) << "Range should contain elements while condition is true";
}

TEST_CASE(TestStartingWithSet)
{
    std::set<int> s = {1, 2, 3, 4, 5};

    auto range = s | std::views::transform([](int x)
                                           { return x * 2; }) |
                 to<std::list<int>>();

    static_assert(std::same_as<decltype(range), std::list<int>>, "Range should be converted to std::set<int>");
    EXPECT_EQ(range, (std::list<int>{2, 4, 6, 8, 10})) << "Range should contain transformed values";
}