// #include <webcraft/async/io/fs.hpp>
// #include <webcraft/async/runtime.hpp>
// #include <webcraft/async/runtime/windows.event.hpp>
// #include <webcraft/async/runtime/macos.event.hpp>
// #include <webcraft/async/runtime/linux.event.hpp>

// using namespace webcraft::async;

// #ifdef __linux__
// #include <unistd.h>

// int ios_to_posix(std::ios_base::openmode mode)
// {
//     int flags = 0;

//     if ((mode & std::ios::in) && (mode & std::ios::out))
//     {
//         flags |= O_RDWR;
//     }
//     else if (mode & std::ios::in)
//     {
//         flags |= O_RDONLY;
//     }
//     else if (mode & std::ios::out)
//     {
//         flags |= O_WRONLY;
//     }

//     if (mode & std::ios::trunc)
//     {
//         flags |= O_TRUNC;
//     }

//     if (mode & std::ios::app)
//     {
//         flags |= O_APPEND;
//     }

//     if (mode & std::ios::out)
//     {
//         flags |= O_CREAT; // often needed with out/app/trunc
//     }

//     return flags;
// }

// struct io_uring_file_read : public webcraft::async::detail::linux::io_uring_runtime_event
// {
//     int fd;
//     std::span<char> buffer;

//     io_uring_file_read(int fd, std::span<char> buffer, std::stop_token token)
//         : webcraft::async::detail::linux::io_uring_runtime_event(token), fd(fd), buffer(buffer)
//     {
//     }

//     void perform_io_uring_operation(struct io_uring_sqe *sqe) override
//     {
//         io_uring_prep_read(sqe, fd, buffer.data(), buffer.size(), 0);
//     }
// };

// struct io_uring_file_write : public webcraft::async::detail::linux::io_uring_runtime_event
// {
//     int fd;
//     std::span<char> buffer;

//     io_uring_file_write(int fd, std::span<char> buffer, std::stop_token token)
//         : webcraft::async::detail::linux::io_uring_runtime_event(token), fd(fd), buffer(buffer)
//     {
//     }

//     void perform_io_uring_operation(struct io_uring_sqe *sqe) override
//     {
//         io_uring_prep_write(sqe, fd, buffer.data(), buffer.size(), 0);
//     }
// };

// class io_uring_file_descriptor : public webcraft::async::io::fs::file_descriptor
// {
// private:
//     int fd;

// public:
//     io_uring_file_descriptor(std::filesystem::path path, std::ios_base::openmode mode) : file_descriptor(mode), path(std::move(path))
//     {
//         fd = open(path.c_str(), ios_to_posix(mode));

//         if (fd < 0)
//         {
//             throw std::runtime_error("Fail to open file");
//         }
//     }

//     ~io_uring_file_descriptor() override
//     {
//         close();
//     }

//     task<size_t> read_bytes(std::span<char> buffer) override
//     {
//         if (get_mode() & std::ios::in)
//         {
//             auto ev = as_awaitable(std::make_unique<io_uring_file_read>(fd, buffer, std::stop_token{}));
//             co_await ev;
//             if (ev.get_result() < 0)
//             {
//                 throw std::runtime_error("Failed to read from file: " + std::string(std::strerror(-ev.get_result())));
//             }
//             co_return ev.get_result();
//         }
//         else
//         {
//             throw std::runtime_error("File not opened for reading");
//         }
//     }

//     task<size_t> write_bytes(std::span<const char> buffer) override
//     {
//         if (get_mode() & std::ios::out)
//         {
//             auto ev = as_awaitable(std::make_unique<io_uring_file_write>(fd, buffer, std::stop_token{}));
//             co_await ev;
//             if (ev.get_result() < 0)
//             {
//                 throw std::runtime_error("Failed to write to file: " + std::string(std::strerror(-ev.get_result())));
//             }
//             co_return ev.get_result();
//         }
//         else
//         {
//             throw std::runtime_error("File not opened for writing");
//         }
//     }

//     void close() override
//     {
//         ::close(fd);
//     }
// };

// std::shared_ptr<webcraft::async::io::fs::file_descriptor> webcraft::async::io::fs::open(const std::filesystem::path &path, std::ios_base::openmode mode)
// {
//     return std::make_shared<io_uring_file_descriptor>(path, mode);
// }

// #elif defined(_WIN32)
// #elif defined(__APPLE__)
// #else

// struct mock_file_descriptor : public webcraft::async::io::fs::file_descriptor
// {
//     mock_file_descriptor(std::ios_base::openmode mode) : file_descriptor(mode)
//     {
//         std::cout << "Mock file descriptor created with mode: " << mode << std::endl;
//     }

//     task<size_t> read_bytes(std::span<char> buffer) override
//     {
//         std::cout << "Mock read_bytes called with buffer size: " << buffer.size() << std::endl;
//         co_return 0; // Simulate no bytes read
//     }

//     task<size_t> write_bytes(std::span<const char> buffer) override
//     {
//         std::cout << "Mock write_bytes called with buffer size: " << buffer.size() << std::endl;
//         // Mock implementation for testing purposes
//         co_return buffer.size(); // Simulate writing all bytes
//     }

//     void close() override
//     {
//         std::cout << "Mock file descriptor closed." << std::endl;
//     }
// };

// std::shared_ptr<webcraft::async::io::fs::file_descriptor> webcraft::async::io::fs::open(const std::filesystem::path &path, std::ios_base::openmode mode)
// {
//     // Here you would normally open the file and return a file descriptor.
//     // For this example, we return a mock file descriptor.
//     return std::make_shared<mock_file_descriptor>(mode);
// }

// #endif