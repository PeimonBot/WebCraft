#pragma once

#include <utility>
#include <concepts>
#include <optional>
#include <coroutine>
#include "task.hpp"
#include "generator.hpp"
#include "async_generator.hpp"

namespace webcraft::async::io
{

    template <typename T>
    concept non_void_v = !std::is_void_v<T>;

    template <non_void_v T>
    class async_readable_stream
    {

    public:
        using value_type = T;

        virtual ~async_readable_stream() = default;

        virtual task<std::optional<T>> recv() = 0;

        task<size_t> recv(std::span<T> buffer)
        {
            if (buffer.empty())
                co_return 0;

            constexpr size_t buffer_size = buffer.size();
            size_t count = 0;
            while (count < buffer_size)
            {
                auto opt_value = co_await recv();
                if (!opt_value)
                    break; // No more data to read

                buffer[count++] = std::move(*opt_value);
            }
            co_return count;
        }

        async_generator<T> to_generator()
        {
            while (true)
            {
                auto value = co_await recv();
                if (!value)
                    break; // No more data to read
                co_yield std::move(*value);
            }
        }

        operator async_generator<T>()
        {
            return to_generator();
        }
    };

    template <non_void_v T>
    class async_writable_stream
    {
    public:
        using value_type = T;

        virtual ~async_writable_stream() = default;

        virtual task<bool> send(T &&value) = 0;
        virtual task<bool> send(const T &value) = 0;

        task<size_t> send(std::span<const T> buffer)
        {
            if (buffer.empty())
                co_return 0;

            size_t count = 0;
            for (const auto &value : buffer)
            {
                bool sent = co_await send(value);
                if (!sent)
                    break; // Failed to send, stop sending more data
                ++count;
            }
            co_return count;
        }
    };

    template <typename StreamType>
    std::unique_ptr<async_readable_stream<StreamType>> to_readable_stream(async_generator<StreamType> gen)
    {

        struct generator_readable_stream : public async_readable_stream<StreamType>
        {
            async_generator<StreamType> generator;

            explicit generator_readable_stream(async_generator<StreamType> gen)
                : generator(std::move(gen)) {}

            task<std::optional<StreamType>> recv() override
            {
                auto it = co_await generator.begin();
                if (it == generator.end())
                {
                    co_return std::nullopt; // No more data
                }
                co_return *it; // Return the current value
            }
        };

        return std::make_unique<generator_readable_stream>(std::move(gen));
    }

    namespace detail
    {
        template <typename StreamType>
        struct channel
        {
            std::unique_ptr<async_readable_stream<StreamType>> source;
            std::shared_ptr<async_writable_stream<StreamType>> sink;
        };

        struct channel_subscription
        {
        };

        template <typename StreamType>
        struct channel_rstream : public async_readable_stream<typename channel<StreamType>::value_type>
        {
            channel<StreamType> chan;

            explicit channel_rstream(channel<StreamType> c)
                : chan(std::move(c)) {}

            task<std::optional<typename channel<StreamType>::value_type>> recv() override
            {
                auto value = co_await chan.source->recv();
                if (value)
                {
                    co_await chan.sink->send(*value);
                }
                co_return value;
            }
        };

        template <typename StreamType>
        struct channel_wstream : public async_writable_stream<typename channel<StreamType>::value_type>
        {
            channel<StreamType> chan;

            explicit channel_wstream(channel<StreamType> c)
                : chan(std::move(c)) {}

            task<bool> send(typename channel<StreamType>::value_type &&value) override
            {
                co_return co_await chan.sink->send(std::move(value));
            }

            task<bool> send(const typename channel<StreamType>::value_type &value) override
            {
                co_return co_await chan.sink->send(value);
            }
        };
    }
}