
#define TEST_SUITE_NAME FilesystemTestSuite

#include "test_suite.hpp"
#include <string>
#include <set>
#include <list>
#include <vector>
#include <webcraft/async/async.hpp>
#include <webcraft/async/io/fs.hpp>

using namespace webcraft::async;
using namespace webcraft::async::io::fs;

TEST_CASE(TestMockFileWrite)
{

    auto fn = []() -> task<void>
    {
        std::string content = "Hello World!";
        file f = make_file("test_write.txt");
        auto stream = f.open_writable_stream();
        auto size = co_await stream.send(content);
        EXPECT_EQ(size, content.size());
    };

    sync_wait(fn());
}

TEST_CASE(TestMockFileRead)
{
    auto fn = []() -> task<void>
    {
        file f = make_file("test_write.txt");
        auto stream = f.open_readable_stream();
        std::array<char, 1024> buffer;
        size_t bytes_read = co_await stream.recv(std::span<char>(buffer));
        EXPECT_GE(bytes_read, 0);
    };

    sync_wait(fn());
}