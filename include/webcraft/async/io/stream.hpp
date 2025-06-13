#pragma once

#include <memory>
#include <span>
#include <webcraft/async/awaitable.hpp>

namespace webcraft::async::io
{
    template <class T>
    class async_istream;
    template <class T>
    class async_ostream;

    template <class T>
    class async_stream;

    template <class T>
    class async_istream
    {
    public:
        virtual ~async_istream() = default;

        virtual task<int> read(std::span<T> buf) = 0;
    };

    template <class T>
    class async_ostream
    {
    public:
        virtual ~async_ostream() = default;
        virtual task<int> write(std::span<T> buf) = 0;
        virtual task<void> flush() = 0;
    };

    template <class T>
    class async_stream : public async_istream<T>, public async_ostream<T>
    {
    };

    template <class T>
    class async_istream_wrapper : public async_istream<T>
    {
    private:
        std::shared_ptr<async_istream<T>> istream;

    public:
        async_istream_wrapper(std::shared_ptr<async_istream<T>> istream) : istream(istream) {}
        ~async_istream_wrapper() = default;

        task<int> read(std::span sp) override
        {
            co_return co_await istream->read(sp);
        }
    };

    template <class T>
    class async_ostream_wrapper : public async_ostream<T>
    {
    private:
        std::shared_ptr<async_ostream<T>> ostream;

    public:
        async_ostream_wrapper(std::shared_ptr<async_ostream<T>> ostream) : ostream(ostream) {}
        ~async_ostream_wrapper() = default;

        task<int> write(std::span<T> sp)
        {
            co_return co_await ostream->write(sp);
        }

        task<void> flush()
        {
            co_await ostream->flush();
        }
    };

}