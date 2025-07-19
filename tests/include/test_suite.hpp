#pragma once
#include <gtest/gtest.h>
#include <random>
#include <cstdint>
#include <chrono>
#include <webcraft/async/event_signal.hpp>
#include <optional>
#include <atomic>
#include <thread>
#include <memory>

using namespace std::chrono_literals;

#ifdef TEST_SUITE_NAME

#define TEST_CASE(name) TEST(TEST_SUITE_NAME, name)

#endif

inline uint64_t generate_random_uint64()
{
    static std::random_device rd;     // Seed source (hardware entropy)
    static std::mt19937_64 gen(rd()); // 64-bit Mersenne Twister engine
    static std::uniform_int_distribution<uint64_t> dist(0, UINT64_MAX);

    return dist(gen);
}

constexpr auto test_timer_timeout = 200ms;
constexpr auto test_cancel_timeout = 100ms;
constexpr auto test_adjustment_factor = 50ms;