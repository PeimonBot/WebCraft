#ifdef __WIN32

#define TEST_SUITE_NAME IOCPTestSuite
#include "test_suite.hpp"

TEST_CASE(SampleTest)
{
    EXPECT_EQ(1 + 1, 2);
}

#endif
