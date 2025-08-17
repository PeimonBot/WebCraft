#define TEST_SUITE_NAME AsyncFSTestSuite

#include "test_suite.hpp"
#include <webcraft/async/async.hpp>
#include <webcraft/async/io/io.hpp>
#include <webcraft/async/runtime.hpp>
#include <filesystem>
#include <sstream>

using namespace webcraft::async;
using namespace webcraft::async::io::adaptors;
using namespace webcraft::async::io::fs;

TEST_CASE(TestFilePathComplatibility)
{
    std::filesystem::path p1("/path/to/file");

    auto f = make_file(p1);

    EXPECT_EQ(p1, f.get_path());

    std::filesystem::path p2 = f;

    EXPECT_EQ(p2, p1);
}

const std::string test_data = "Hello, World!\r\nThis is some test data that is in the file\r\nWe need to use some kind of test procedure so we decided to go with this.\r\n";
const std::filesystem::path test_file_path = "test_file.txt";

void create_and_populate_test_file()
{
    std::ofstream ofs(test_file_path);
    ofs << test_data;
    ofs.close();
}

void cleanup_test_file()
{
    std::filesystem::remove(test_file_path);
}

std::string get_test_file_contents()
{
    std::ifstream ifs(test_file_path);
    std::stringstream buffer;
    buffer << ifs.rdbuf();
    return buffer.str();
}

TEST_CASE(TestFileReadableStreamDoesNotTruncate)
{
    runtime_context context;

    create_and_populate_test_file();

    auto f = make_file(test_file_path);

    auto task_fn = [&]() -> task<void>
    {
        auto stream = co_await f.open_readable_stream();
    };

    sync_wait(task_fn());

    std::string content = get_test_file_contents();

    EXPECT_EQ(content, test_data) << "File contents should not be truncated";

    cleanup_test_file();
}

TEST_CASE(TestFileWritableStreamDoesTruncate)
{
    runtime_context context;

    create_and_populate_test_file();

    auto f = make_file(test_file_path);

    auto task_fn = [&]() -> task<void>
    {
        auto stream = co_await f.open_writable_stream();
    };

    sync_wait(task_fn());

    std::string content = get_test_file_contents();

    EXPECT_EQ(content, "") << "File contents should be truncated";

    cleanup_test_file();
}

TEST_CASE(TestFileAppendableStreamDoesNotTruncate)
{
    runtime_context context;

    create_and_populate_test_file();

    auto f = make_file(test_file_path);

    auto task_fn = [&]() -> task<void>
    {
        auto stream = co_await f.open_writable_stream(true);
    };

    sync_wait(task_fn());

    std::string content = get_test_file_contents();

    EXPECT_EQ(content, test_data) << "File contents should be truncated";

    cleanup_test_file();
}

TEST_CASE(TestFileReadAll)
{
    runtime_context context;

    create_and_populate_test_file();

    auto f = make_file(test_file_path);

    auto task_fn = [&]() -> task<void>
    {
        auto stream = co_await f.open_readable_stream();

        std::vector<char> content;
        std::array<char, 1024> buffer;

        while (auto bytes_read = co_await stream.recv(buffer))
        {
            content.insert(content.end(), buffer.begin(), buffer.begin() + bytes_read);
        }

        std::string_view str(content.begin(), content.end());
        EXPECT_EQ(str, test_data) << "File contents should be the same";
    };

    sync_wait(task_fn());

    cleanup_test_file();
}

TEST_CASE(TestFileReadAllUsingAdaptors)
{
    runtime_context context;

    create_and_populate_test_file();

    auto f = make_file(test_file_path);

    auto task_fn = [&]() -> task<void>
    {
        auto stream = co_await f.open_readable_stream();

        std::vector<char> content = co_await (stream | collect<std::vector<char>, char>(collectors::to_vector<char>()));

        std::string_view str(content.begin(), content.end());
        EXPECT_EQ(str, test_data) << "File contents should be the same";
    };

    sync_wait(task_fn());

    cleanup_test_file();
}

TEST_CASE(TestFileWriteAll)
{
    runtime_context context;

    auto f = make_file(test_file_path);

    auto task_fn = [&]() -> task<void>
    {
        auto stream = co_await f.open_writable_stream();
        std::string data = test_data;
        co_await stream.send(data);
    };

    sync_wait(task_fn());

    std::string content = get_test_file_contents();

    EXPECT_EQ(content, test_data) << "File contents should be the same";

    cleanup_test_file();
}