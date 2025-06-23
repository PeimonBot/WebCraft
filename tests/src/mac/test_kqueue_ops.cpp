#ifdef __APPLE__

#define TEST_SUITE_NAME KqueueTestSuite
#include "test_suite.hpp"

TEST_CASE(SampleTest)
{
    EXPECT_EQ(1 + 1, 2);
}

#endif