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
