#pragma once

#include "core"

namespace webcraft::async::io::adaptors
{

    template <typename Derived>
    struct stream_adaptor_closure
    {
        template <typename StreamType>
        friend auto operator|(std::unique_ptr<async_readable_stream<StreamType>> &&stream, Derived &&self)
        {
            return self(stream);
        }

        template <typename StreamType>
        friend auto operator|(std::unique_ptr<async_readable_stream<StreamType>> &&stream, Derived &self)
        {
            return self(stream);
        }

        template <typename StreamType>
        friend auto operator|(std::unique_ptr<async_readable_stream<StreamType>> &&stream, const Derived &self)
        {
            return self(stream);
        }
    };

    namespace detail
    {

        template <typename StreamType>
        struct pipe_adaptor_closure : public stream_adaptor_closure<pipe_adaptor_closure<StreamType>>
        {
            std::shared_ptr<async_writable_stream<StreamType>> sink;

            explicit pipe_adaptor_closure(std::shared_ptr<async_writable_stream<StreamType>> sink)
                : sink(std::move(sink)) {}

            std::unique_ptr<async_readable_stream<StreamType>> operator()(std::unique_ptr<async_readable_stream<StreamType>> stream) const
            {

                struct tee_rstream : public async_readable_stream<StreamType>
                {
                    std::unique_ptr<async_readable_stream<StreamType>> source;
                    std::shared_ptr<async_writable_stream<StreamType>> sink;

                    tee_rstream(std::unique_ptr<async_readable_stream<StreamType>> src, std::shared_ptr<async_writable_stream<StreamType>> snk)
                        : source(std::move(src)), sink(std::move(snk)) {}

                    task<std::optional<StreamType>> recv() override
                    {
                        auto value = co_await source->recv();
                        if (value)
                        {
                            co_await sink->send(*value);
                        }
                        co_return value;
                    }
                };
                return std::make_unique<tee_rstream>(std::move(stream), sink);
            }
        };

        template <typename FromType, typename Func>
        struct map_adaptor_closure : public stream_adaptor_closure<map_adaptor_closure<FromType, Func>>
        {
            static_assert(std::is_invocable_v<Func, FromType>,
                          "Transform function must be invocable with FromType");

            using result_type = std::invoke_result_t<Func, FromType>;
            static_assert(non_void_v<result_type>,
                          "Transform function must return a non-void type");

            Func func;

            explicit map_adaptor_closure(Func f) : func(std::move(f)) {}

            std::unique_ptr<async_readable_stream<result_type>> operator()(std::unique_ptr<async_readable_stream<FromType>> stream) const
            {
                struct transform_rstream : public async_readable_stream<result_type>
                {
                    std::unique_ptr<async_readable_stream<FromType>> source;
                    Func func;

                    transform_rstream(std::unique_ptr<async_readable_stream<FromType>> src, Func f)
                        : source(std::move(src)), func(std::move(f)) {}

                    task<std::optional<result_type>> recv() override
                    {
                        auto value = co_await source->recv();
                        if (value)
                        {
                            co_return func(*value);
                        }
                        co_return std::nullopt;
                    }
                };
                return std::make_unique<transform_rstream>(std::move(stream), func);
            }
        };

        template <typename StreamType>
        struct for_each_adaptor_closure : public stream_adaptor_closure<for_each_adaptor_closure<StreamType>>
        {
            using value_type = StreamType;
            std::function<void(const StreamType &)> func;

            explicit for_each_adaptor_closure(std::function<void(const StreamType &)> f)
                : func(std::move(f)) {}

            task<void> operator()(std::unique_ptr<async_readable_stream<StreamType>> stream) const
            {
                while (auto opt = co_await stream->recv())
                {
                    func(*opt);
                }
            }
        };

        template <typename FromType, typename ToType>
        struct transform_adaptor_closure : public stream_adaptor_closure<transform_adaptor_closure<FromType, ToType>>
        {
            std::function<async_generator<ToType>(async_generator<FromType>)> func;

            explicit transform_adaptor_closure(std::function<async_generator<ToType>(async_generator<FromType>)> f)
                : func(std::move(f)) {}

            std::unique_ptr<async_readable_stream<ToType>> operator()(std::unique_ptr<async_readable_stream<FromType>> stream) const
            {
                auto gen = func(to_generator(std::move(stream)));
                return to_readable_stream(std::move(gen));
            }
        };

    }

    template <typename StreamType>
    auto pipe(std::shared_ptr<async_writable_stream<StreamType>> sink)
    {
        return detail::pipe_adaptor_closure<StreamType>{std::move(sink)};
    }

    template <typename FromType, typename Func>
    auto map(Func func)
    {
        return detail::map_adaptor_closure<FromType, Func>{std::move(func)};
    }

    template <typename StreamType>
    auto for_each(std::function<void(const StreamType &)> func)
    {
        return detail::for_each_adaptor_closure<StreamType>{std::move(func)};
    }

    template <typename FromType, typename ToType>
    auto transform(std::function<async_generator<ToType>(async_generator<FromType>)> func)
    {
        return detail::transform_adaptor_closure<FromType, ToType>{std::move(func)};
    }

    // rest can be done with transform due to how powerful it is
    template <typename StreamType>
    auto filter(std::function<bool(const StreamType &)> predicate)
    {
        auto generator_func = [predicate = std::move(predicate)](async_generator<StreamType> gen) -> async_generator<StreamType>
        {
            for (auto it = co_await gen.begin(); it != gen.end(); co_await ++it)
            {
                if (predicate(*it))
                {
                    co_yield *it;
                }
            }
        };

        return transform<StreamType, StreamType>(std::move(generator_func));
    }

    template <typename StreamType>
    auto take_while(std::function<bool(const StreamType &)> predicate)
    {
        auto generator_func = [predicate = std::move(predicate)](async_generator<StreamType> gen) -> async_generator<StreamType>
        {
            for (auto it = co_await gen.begin(); it != gen.end(); co_await ++it)
            {
                if (!predicate(*it))
                {
                    break; // Stop yielding when predicate fails
                }
                co_yield *it;
            }
        };

        return transform<StreamType, StreamType>(std::move(generator_func));
    }

    template <typename StreamType>
    auto drop_while(std::function<bool(const StreamType &)> predicate)
    {
        auto generator_func = [predicate = std::move(predicate)](async_generator<StreamType> gen) -> async_generator<StreamType>
        {
            bool should_drop = true;
            for (auto it = co_await gen.begin(); it != gen.end(); co_await ++it)
            {
                if (should_drop && predicate(*it))
                {
                    continue; // Skip this value
                }
                should_drop = false; // Start yielding after the first non-matching value
                co_yield *it;
            }
        };

        return transform<StreamType, StreamType>(std::move(generator_func));
    }

    template <typename StreamType>
    auto take(size_t count)
    {
        auto generator_func = [count](async_generator<StreamType> gen) -> async_generator<StreamType>
        {
            size_t yielded = 0;
            for (auto it = co_await gen.begin(); it != gen.end() && yielded < count; co_await ++it)
            {
                co_yield *it;
                ++yielded;
            }
        };

        return transform<StreamType, StreamType>(std::move(generator_func));
    }

    template <typename StreamType>
    auto drop(size_t count)
    {
        auto generator_func = [count](async_generator<StreamType> gen) -> async_generator<StreamType>
        {
            size_t skipped = 0;
            for (auto it = co_await gen.begin(); it != gen.end(); co_await ++it)
            {
                if (skipped < count)
                {
                    ++skipped; // Skip this value
                    continue;
                }
                co_yield *it;
            }
        };

        return transform<StreamType, StreamType>(std::move(generator_func));
    }

    template <typename Derived, typename ToType, typename StreamType>
    concept collector = requires(Derived t, async_generator<StreamType> gen) {
        { t(gen) } -> std::convertible_to<task<ToType>>;
    };

    template <typename ToType, typename StreamType, collector<ToType, StreamType> Collector>
    auto collect(Collector collector_func)
    {
        return [collector_func = std::move(collector_func)](async_generator<StreamType> gen) -> task<ToType>
        {
            co_return co_await collector_func(std::move(gen));
        }();
    }

    template <std::ranges::input_range StreamType>
    auto flatten()
    {
        return detail::transform_adaptor_closure<StreamType, typename StreamType::value_type>(
            [](async_generator<StreamType> gen) -> async_generator<typename StreamType::value_type>
            {
                for (auto it = co_await gen.begin(); it != gen.end(); co_await ++it)
                {
                    for (auto inner_it = co_await (*it).begin(); inner_it != (*it).end(); ++inner_it)
                    {
                        co_yield *inner_it;
                    }
                }
            });
    }

    template <typename StreamType>
    auto to(async_writable_stream<StreamType> &sink)
    {
        auto collector = [&sink](async_generator<StreamType> gen) -> task<void>
        {
            for (auto it = co_await gen.begin(); it != gen.end(); co_await ++it)
            {
                bool sent = co_await sink.send(*it);
                if (!sent)
                {
                    break; // Stop sending if the sink cannot accept more data
                }
            }
            co_return;
        };

        return collect(collector);
    }

    template <typename T>
    auto enumerate()
    {
        return transform<T, std::pair<size_t, T>>(
            [](async_generator<T> gen) -> async_generator<std::pair<size_t, T>>
            {
                size_t index = 0;
                for (auto it = co_await gen.begin(); it != gen.end(); co_await ++it)
                {
                    co_yield {index++, *it};
                }
            });
    }

    template <typename T>
    auto chunk(size_t size)
    {
        return transform<T, std::vector<T>>(
            [size](async_generator<T> gen) -> async_generator<std::vector<T>>
            {
                std::vector<T> buffer;
                buffer.reserve(size);
                for (auto it = co_await gen.begin(); it != gen.end(); co_await ++it)
                {
                    buffer.push_back(*it);
                    if (buffer.size() == size)
                    {
                        co_yield std::move(buffer);
                        buffer.clear();
                        buffer.reserve(size);
                    }
                }
                if (!buffer.empty())
                {
                    co_yield std::move(buffer);
                }
            });
    }

    template <typename StreamType>
    auto count()
    {
        return collect<StreamType, size_t>(
            [](async_generator<StreamType> gen) -> task<size_t>
            {
                size_t count = 0;
                for (auto it = co_await gen.begin(); it != gen.end(); co_await ++it)
                {
                    ++count;
                }
                co_return count;
            });
    }

    enum class zip_strategy
    {
        FULL,
        LEFT,
        RIGHT,
        INNER
    };

    static bool allows_left_zip(zip_strategy strat)
    {
        return strat == zip_strategy::FULL || strat == zip_strategy::LEFT;
    }

    static bool allows_right_zip(zip_strategy strat)
    {
        return strat == zip_strategy::FULL || strat == zip_strategy::RIGHT;
    }

    template <typename StreamType>
    auto zip(std::unique_ptr<StreamType> other, zip_strategy strat = zip_strategy::FULL)
    {
        return transform<StreamType, std::pair<std::optional<StreamType>, std::optional<StreamType>>>(
            [other = std::move(other), strat](async_generator<StreamType> gen) -> async_generator<std::pair<std::optional<StreamType>, std::optional<StreamType>>>
            {
                auto it1 = co_await gen.begin();
                auto it2 = co_await other->begin();

                while (it1 != gen.end() && it2 != other->end())
                {
                    co_yield {*it1, *it2};
                    co_await ++it1;
                    co_await ++it2;
                }
                while (it1 != gen.end() && allows_left_zip(strat))
                {
                    co_yield {*it1, std::nullopt};
                    co_await ++it1;
                }
                while (it2 != other->end() && allows_right_zip(strat))
                {
                    co_yield {std::nullopt, *it2};
                    co_await ++it2;
                }
            });
    }

    namespace collectors
    {
        // TODO: To be done, make basic collectors like to_vector, to_set, to_string, joining, foldLeft, foldRight, etc.
    }
}