#pragma once

#include "core.hpp"
#include <fstream>
#include <filesystem>
#include <atomic>
#include <webcraft/async/fire_and_forget_task.hpp>

namespace webcraft::async::io::fs
{
    namespace detail
    {

        class file_descriptor
        {
        protected:
            std::ios_base::openmode mode;

        public:
            file_descriptor(std::ios_base::openmode mode) : mode(mode) {}
            virtual ~file_descriptor() = default;

            // virtual because we want to allow platform specific implementation
            virtual task<size_t> read(std::span<char> buffer) = 0;  // internally should check if openmode is for read
            virtual task<size_t> write(std::span<char> buffer) = 0; // internally should check if openmode is for write or append
            virtual task<void> close() = 0;                         // will spawn a fire and forget task (essentially use async apis but provide null callback)
        };

        task<std::shared_ptr<file_descriptor>> make_file_descriptor(std::filesystem::path p, std::ios_base::openmode mode);

        class file_stream
        {
        protected:
            std::shared_ptr<file_descriptor> fd;
            std::atomic<bool> closed{false};

        public:
            explicit file_stream(std::shared_ptr<file_descriptor> fd) : fd(std::move(fd)) {}
            file_stream(file_stream &&other) noexcept : fd(std::exchange(other.fd, nullptr)), closed(other.closed.exchange(true)) {}
            file_stream &operator=(file_stream &&other) noexcept
            {
                if (this != &other)
                {
                    fd = std::exchange(other.fd, nullptr);
                    closed = other.closed.exchange(true);
                }
                return *this;
            }
            virtual ~file_stream() noexcept
            {
                if (fd)
                    fire_and_forget(close());
            }

            task<void> close() noexcept
            {
                bool expected = false;
                if (closed.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                {
                    co_await fd->close();
                }
            }
        };
    }

    class file_rstream : public detail::file_stream
    {
    public:
        explicit file_rstream(std::shared_ptr<detail::file_descriptor> fd) : detail::file_stream(std::move(fd)) {}
        ~file_rstream() noexcept = default;

        file_rstream(file_rstream &&rstream) noexcept = default;
        file_rstream &operator=(file_rstream &&) noexcept = default;
        file_rstream(const file_rstream &) = delete;
        file_rstream &operator=(const file_rstream &) = delete;

        task<size_t> recv(std::span<char> buffer)
        {
            return fd->read(buffer);
        }

        task<std::optional<char>> recv()
        {
            std::array<char, 1> buf;
            if (co_await recv(buf))
            {
                co_return buf[0];
            }
            co_return std::nullopt;
        }
    };

    static_assert(async_readable_stream<file_rstream, char>);
    static_assert(async_buffered_readable_stream<file_rstream, char>);
    static_assert(async_closeable_stream<file_rstream, char>);

    class file_wstream : public detail::file_stream
    {
    public:
        explicit file_wstream(std::shared_ptr<detail::file_descriptor> fd) : detail::file_stream(std::move(fd)) {}
        ~file_wstream() noexcept = default;

        file_wstream(file_wstream &&) noexcept = default;
        file_wstream &operator=(file_wstream &&) noexcept = default;
        file_wstream(const file_wstream &) = delete;
        file_wstream &operator=(const file_wstream &) = delete;

        task<size_t> send(std::span<char> buffer)
        {
            return fd->write(buffer);
        }

        task<bool> send(char b)
        {
            std::array<char, 1> buf;
            buf[0] = b;
            if (co_await send(buf))
            {
                co_return true;
            }
            co_return false;
        }
    };

    static_assert(async_writable_stream<file_wstream, char>);
    static_assert(async_buffered_writable_stream<file_wstream, char>);
    static_assert(async_closeable_stream<file_wstream, char>);

    class file
    {
    private:
        std::filesystem::path p;

    public:
        file(std::filesystem::path p) : p(std::move(p)) {}
        ~file() = default;

        task<file_rstream> open_readable_stream()
        {
            auto descriptor = co_await detail::make_file_descriptor(p, std::ios_base::in);
            co_return file_rstream(descriptor);
        }

        task<file_wstream> open_writable_stream(bool append = false)
        {
            auto descriptor = co_await detail::make_file_descriptor(p, std::ios_base::out | (append ? std::ios_base::app : std::ios_base::trunc));
            co_return file_wstream(descriptor);
        }

        constexpr const std::filesystem::path get_path() const { return p; }
        constexpr operator const std::filesystem::path &() const { return p; }
    };

    inline file make_file(std::filesystem::path p)
    {
        return file(p);
    }
}