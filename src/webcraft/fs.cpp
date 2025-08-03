#include <webcraft/async/io/fs.hpp>

using namespace webcraft::async;

struct mock_file_descriptor : public webcraft::async::io::fs::file_descriptor
{
    mock_file_descriptor(std::ios_base::openmode mode) : file_descriptor(mode)
    {
        std::cout << "Mock file descriptor created with mode: " << mode << std::endl;
    }

    task<size_t> read_bytes(std::span<char> buffer) override
    {
        std::cout << "Mock read_bytes called with buffer size: " << buffer.size() << std::endl;
        co_return 0; // Simulate no bytes read
    }

    task<size_t> write_bytes(std::span<const char> buffer) override
    {
        std::cout << "Mock write_bytes called with buffer size: " << buffer.size() << std::endl;
        // Mock implementation for testing purposes
        co_return buffer.size(); // Simulate writing all bytes
    }

    void close() override
    {
        std::cout << "Mock file descriptor closed." << std::endl;
    }
};

std::shared_ptr<webcraft::async::io::fs::file_descriptor> webcraft::async::io::fs::open(const std::filesystem::path &path, std::ios_base::openmode mode)
{
    // Here you would normally open the file and return a file descriptor.
    // For this example, we return a mock file descriptor.
    return std::make_shared<mock_file_descriptor>(mode);
}