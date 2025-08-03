#pragma once

#include "core.hpp"
#include <fstream>
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
    };

    std::shared_ptr<file_descriptor> open(const std::filesystem::path &path, std::ios_base::openmode mode);

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
    private:
        std::filesystem::path file_path;

    public:
        explicit file(const std::filesystem::path &path) : file_path(path) {}

        bool exists() const
        {
            return std::filesystem::exists(file_path);
        }
        bool is_directory() const
        {
            return std::filesystem::is_directory(file_path);
        }
        bool is_regular_file() const
        {
            return std::filesystem::is_regular_file(file_path);
        }
        bool is_symlink() const
        {
            return std::filesystem::is_symlink(file_path);
        }

        std::uintmax_t size() const
        {
            return std::filesystem::file_size(file_path);
        }

        bool create_if_not_exists() const
        {
            if (!exists())
            {
                std::ofstream ofs(file_path);
            }
            return exists();
        }

        bool mkdir() const
        {
            return std::filesystem::create_directory(file_path);
        }
        bool delete_file() const
        {
            return std::filesystem::remove(file_path);
        }

        std::filesystem::file_time_type last_write_time() const
        {
            return std::filesystem::last_write_time(file_path);
        }

        std::filesystem::file_type type() const
        {
            return std::filesystem::status(file_path).type();
        }

        std::filesystem::path path() const
        {
            return file_path;
        }

        file_readable_stream open_readable_stream() const
        {
            auto fd = open(file_path, std::ios_base::in);
            if (!fd)
                throw std::runtime_error("Failed to open file for reading: " + file_path.string());
            return file_readable_stream(fd);
        }
        file_writable_stream open_writable_stream(bool append = false) const
        {
            auto mode = std::ios_base::out | (append ? std::ios_base::app : std::ios_base::trunc);
            auto fd = open(file_path, mode);
            if (!fd)
                throw std::runtime_error("Failed to open file for writing: " + file_path.string());
            return file_writable_stream(fd);
        }
    };

    inline file make_file(const std::filesystem::path &path)
    {
        return file(path);
    }

    inline task<std::string> read_file(const std::filesystem::path &path)
    {
        std::vector<char> source;
        auto file = make_file(path);
        if (!file.exists() || !file.is_regular_file())
        {
            throw std::runtime_error("File does not exist or is not a regular file: " + path.string());
        }

        auto readable_stream = file.open_readable_stream();
        source.resize(file.size());
        size_t bytes_read = 0;
        while (bytes_read < source.size())
        {
            auto chunk = co_await readable_stream.recv(std::span<char>(source.data() + bytes_read, source.size() - bytes_read));
            if (chunk == 0)
            {
                break; // EOF
            }
            bytes_read += chunk;
        }
        co_return std::string(source.data(), bytes_read);
    }

    inline task<void> write_file(const std::filesystem::path &path, const std::string &content, bool append = false)
    {
        auto file = make_file(path);

        if (file.exists() && file.is_directory())
        {
            throw std::runtime_error("Cannot write to a directory: " + path.string());
        }

        auto writable_stream = file.open_writable_stream(append);
        size_t bytes_written = 0;
        while (bytes_written < content.size())
        {
            auto chunk = co_await writable_stream.send(std::span<const char>(content.data() + bytes_written, content.size() - bytes_written));
            if (chunk == 0)
            {
                break; // EOF
            }
            bytes_written += chunk;
        }
        if (bytes_written < content.size())
        {
            throw std::runtime_error("Failed to write all bytes to file: " + path.string());
        }
    }

    inline task<void> copy_file(const std::filesystem::path &source, const std::filesystem::path &destination)
    {
        auto source_file = make_file(source);
        if (!source_file.exists() || !source_file.is_regular_file())
        {
            throw std::runtime_error("Source file does not exist or is not a regular file: " + source.string());
        }

        auto destination_file = make_file(destination);
        if (destination_file.exists() && destination_file.is_directory())
        {
            throw std::runtime_error("Destination path is a directory: " + destination.string());
        }

        destination_file.create_if_not_exists();

        auto readable_stream = source_file.open_readable_stream();
        auto writable_stream = destination_file.open_writable_stream();

        std::vector<char> buffer(8192); // 8 KB buffer
        size_t bytes_read;
        while ((bytes_read = co_await readable_stream.recv(std::span<char>(buffer))) > 0)
        {
            co_await writable_stream.send(std::span<const char>(buffer.data(), bytes_read));
        }
    }

    inline task<void> move_file(const std::filesystem::path &source, const std::filesystem::path &destination)
    {
        co_await webcraft::async::io::fs::copy_file(source, destination);
        make_file(source).delete_file();
    }

}