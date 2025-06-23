#pragma once
#include <gtest/gtest.h>
#include <random>
#include <cstdint>

#ifdef TEST_SUITE_NAME

#define TEST_CASE(name) TEST(TEST_SUITE_NAME, name)

#endif

uint64_t generate_random_uint64()
{
    static std::random_device rd;     // Seed source (hardware entropy)
    static std::mt19937_64 gen(rd()); // 64-bit Mersenne Twister engine
    static std::uniform_int_distribution<uint64_t> dist(0, UINT64_MAX);

    return dist(gen);
}