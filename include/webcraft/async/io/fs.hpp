#pragma once

#include "core.hpp"
#include <filesystem>

namespace webcraft::async::io::fs
{

    struct file_descriptor
    {
    private:
        std::ios_base::openmode mode;

    public:
        file_descriptor(std::ios_base::openmode mode) : mode(mode) {}
        file_descriptor(file_descriptor &&) = default;
        file_descriptor &operator=(file_descriptor &&) = default;

        virtual ~file_descriptor() = default;
        virtual task<size_t> read_bytes(std::span<char> buffer) = 0;
        virtual task<size_t> write_bytes(std::span<const char> buffer) = 0;
        virtual void close() = 0;

        std::ios_base::openmode get_mode() const { return mode; }

        static std::shared_ptr<file_descriptor> open(const std::filesystem::path &path, std::ios_base::openmode mode);
    };

    struct file_readable_stream
    {
    private:
        std::shared_ptr<file_descriptor> fd;

    public:
        explicit file_readable_stream(std::shared_ptr<file_descriptor> fd) : fd(fd) {}
        task<std::optional<char>> recv()
        {
            std::array<char, 1> buffer;
            auto bytes_read = co_await recv(buffer);
            if (bytes_read == 0)
            {
                co_return std::nullopt;
            }
            co_return buffer[0];
        }

        task<size_t> recv(std::span<char> buffer)
        {
            return fd->read_bytes(buffer);
        }
    };

    struct file_writable_stream
    {
    private:
        std::shared_ptr<file_descriptor> fd;

    public:
        explicit file_writable_stream(std::shared_ptr<file_descriptor> fd) : fd(fd) {}

        task<bool> send(char c)
        {
            std::array<char, 1> buffer = {c};
            size_t sent = co_await send(buffer);
            co_return sent == 1;
        }

        task<size_t> send(std::span<const char> buffer)
        {
            return fd->write_bytes(buffer);
        }
    };

    static_assert(async_readable_stream<file_readable_stream, char>, "file_readable_stream should be an async readable stream");
    static_assert(async_writable_stream<file_writable_stream, char>, "file_writable_stream should be an async writable stream");

    struct file
    {

        bool exists() const;
        bool is_directory() const;
        bool is_regular_file() const;
        bool is_symlink() const;

        std::uintmax_t size() const;

        bool create_if_not_exists() const;
        bool mkdir() const;
        bool delete_file() const;

        std::filesystem::file_time_type last_write_time() const;
        std::filesystem::file_time_type last_access_time() const;
        std::filesystem::file_time_type creation_time() const;
        std::filesystem::file_type type() const;

        std::filesystem::path path() const;

        file_readable_stream open_readable_stream() const;
        file_writable_stream open_writable_stream(bool append) const;
    };

    file make_file(const std::filesystem::path &path);

    task<std::string> read_file(const std::filesystem::path &path);
    task<void> write_file(const std::filesystem::path &path, const std::string &content, bool append = false);
    task<void> copy_file(const std::filesystem::path &source, const std::filesystem::path &destination);
    inline task<void> move_file(const std::filesystem::path &source, const std::filesystem::path &destination)
    {
        co_await copy_file(source, destination);
        make_file(source).delete_file();
    }

}