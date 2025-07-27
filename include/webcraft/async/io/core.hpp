#pragma once

#include <utility>
#include <concepts>
#include <optional>
#include <coroutine>
#include "task.hpp"
#include "generator.hpp"
#include "async_generator.hpp"
#include <mutex>

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
        struct mpsc_channel_subscription
        {
            std::deque<StreamType> queue;
            std::coroutine_handle<> waiter;
            std::mutex mutex;

            task<std::optional<StreamType>> get_next()
            {
                struct awaiter
                {
                    mpsc_channel_subscription *self;

                    bool await_ready() const noexcept
                    {
                        std::scoped_lock lock(self->mutex);
                        return !self->queue.empty();
                    }

                    void await_suspend(std::coroutine_handle<> handle)
                    {
                        std::scoped_lock lock(self->mutex);
                        self->waiter = handle;
                    }

                    std::optional<StreamType> await_resume()
                    {
                        std::scoped_lock lock(self->mutex);
                        if (!self->queue.empty())
                        {
                            auto val = std::move(self->queue.front());
                            self->queue.pop_front();
                            return val;
                        }
                        return std::nullopt;
                    }
                };

                co_return co_await awaiter{this};
            }

            task<bool> set_next(StreamType &&value)
            {
                std::unique_lock lock(mutex);
                if (waiter && !waiter.done())
                {
                    auto h = std::exchange(waiter, {});
                    queue.push_back(std::move(value));
                    h.resume();
                    co_return true;
                }
                else
                {
                    queue.push_back(std::move(value));
                    co_return true;
                }
            }
        };

        template <typename StreamType>
        struct mpsc_channel_rstream : public async_readable_stream<StreamType>
        {
            std::shared_ptr<mpsc_channel_subscription<StreamType>> chan;

            explicit mpsc_channel_rstream(std::shared_ptr<mpsc_channel_subscription<StreamType>> c)
                : chan(c) {}

            task<std::optional<StreamType>> recv() override
            {
                co_return co_await chan->get_next();
            }
        };

        template <typename StreamType>
        struct mpsc_channel_wstream : public async_writable_stream<StreamType>
        {
            std::shared_ptr<mpsc_channel_subscription<StreamType>> chan;

            explicit mpsc_channel_wstream(std::shared_ptr<mpsc_channel_subscription<StreamType>> c)
                : chan(c) {}

            task<bool> send(StreamType &&value) override
            {
                co_return co_await chan->set_next(std::move(value));
            }

            task<bool> send(const StreamType &value) override
            {
                co_return co_await chan->set_next(value);
            }
        };
    }

    template <typename StreamType>
    std::pair<std::unique_ptr<async_readable_stream<StreamType>>, std::unique_ptr<async_writable_stream<StreamType>>> make_mpsc_channel()
    {
        auto subscription = std::make_shared<detail::mpsc_channel_subscription<StreamType>>();
        auto rstream = std::make_unique<detail::mpsc_channel_rstream<StreamType>>(subscription);
        auto wstream = std::make_unique<detail::mpsc_channel_wstream<StreamType>>(subscription);
        return {std::move(rstream), std::move(wstream)};
    }
}