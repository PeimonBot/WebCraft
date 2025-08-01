#pragma once

#include <concepts>
#include <type_traits>
#include "core.hpp"

namespace webcraft::async::io::adaptors
{

    template <typename Derived, typename T>
    struct async_readable_stream_adaptor
    {

        friend auto operator|(async_readable_stream<T> auto &&stream, Derived &adaptor)
        {
            return std::invoke(adaptor, std::move(stream));
        }

        friend auto operator|(async_readable_stream<T> auto &&stream, Derived &&adaptor)
        {
            return std::invoke(std::move(adaptor), std::forward<decltype(stream)>(stream));
        }
    };

    namespace detail
    {
        template <typename InType, typename Func, typename OutType = typename std::invoke_result_t<Func, async_generator<InType>>::value_type>
        class transform_stream_adaptor : public async_readable_stream_adaptor<transform_stream_adaptor<InType, Func, OutType>, InType>
        {
        private:
            Func transform_fn;

        public:
            static_assert(std::is_invocable_r_v<async_generator<OutType>, Func, async_generator<InType>>,
                          "Function must accept an async_generator<InType> and return an async_generator<OutType>");

            explicit transform_stream_adaptor(Func &&fn)
                : transform_fn(std::move(fn)) {}

            async_readable_stream<OutType> auto operator()(async_readable_stream<InType> auto &&stream) const
            {
                auto &&gen = to_async_generator<InType>(std::move(stream));
                auto &&new_gen = transform_fn(std::move(gen));
                return to_readable_stream<OutType>(std::move(new_gen));
            }
        };

    }

    template <typename InType, typename Func>
    auto transform(Func &&fn)
    {
        static_assert(std::is_invocable_v<Func, async_generator<InType>>,
                      "Function must accept an async_generator<InType> and return an async_generator<OutType>");

        return detail::transform_stream_adaptor<InType, Func>(std::move(fn));
    }

    template <typename InType, typename OutType>
    auto map(std::function<OutType(InType)> &&fn)
    {
        return transform<InType>([fn = std::move(fn)](async_generator<InType> gen) -> async_generator<OutType>
                                 { for_each_async(value, gen,
                                                  {
                                                      co_yield fn(std::move(value));
                                                  }); });
    }

    template <typename T>
        requires std::is_copy_assignable_v<T>
    auto pipe(async_writable_stream<T> auto &str)
    {
        return transform<T>([&str](async_generator<T> gen) -> async_generator<T>
                            { for_each_async(value, gen,
                                             {
                                                 T backup = value;
                                                 co_await str.send(std::move(value)); // Send a copy of value
                                                 co_yield std::move(backup);
                                             }); });
    }

    template <typename Derived, typename ToType, typename StreamType>
    concept collector = std::is_invocable_r_v<task<ToType>, Derived, async_generator<StreamType>>;

    namespace detail
    {

        template <typename ToType, typename StreamType, collector<ToType, StreamType> Collector>
        class collector_stream_adaptor : public async_readable_stream_adaptor<collector_stream_adaptor<ToType, StreamType, Collector>, StreamType>
        {
        private:
            Collector collector_fn;

        public:
            explicit collector_stream_adaptor(Collector &&collector_fn) : collector_fn(std::move(collector_fn)) {}

            task<ToType> operator()(async_readable_stream<StreamType> auto &&stream) const
            {
                auto &&gen = to_async_generator<StreamType>(std::move(stream));
                co_return co_await collector_fn(std::move(gen));
            }
        };
    }

    template <typename ToType, typename StreamType, collector<ToType, StreamType> Collector>
    auto collect(Collector &&collector_func)
    {
        return detail::collector_stream_adaptor<ToType, StreamType, Collector>(std::move(collector_func));
    }

    template <typename T>
    auto forward_to(async_writable_stream<T> auto &stream)
    {
        auto collector_func = [&stream](async_generator<T> gen) -> task<void>
        {
            for_each_async(value, gen,
                           {
                               co_await stream.send(std::move(value));
                           });
        };

        return collect<void, T>(std::move(collector_func));
    }

    template <typename T>
    auto filter(std::function<bool(const T &)> &&predicate)
    {
        return transform<T>([predicate = std::move(predicate)](async_generator<T> gen) -> async_generator<T>
                            { for_each_async(value, gen,
                                             {
                                                 if (predicate(value))
                                                 {
                                                     co_yield std::move(value);
                                                 }
                                             }); });
    }

    template <typename T>
    auto take(size_t count)
    {
        return transform<T>([count](async_generator<T> gen) -> async_generator<T>
                            {
                                size_t taken = 0;
                                for_each_async(value, gen,
                                               {
                                                   if (taken < count)
                                                   {
                                                       co_yield std::move(value);
                                                       ++taken;
                                                   }
                                               }); });
    }

    template <typename T>
    auto drop(size_t count)
    {
        return transform<T>([count](async_generator<T> gen) -> async_generator<T>
                            {
                                size_t dropped = 0;
                                for_each_async(value, gen,
                                               {
                                                   if (dropped < count)
                                                   {
                                                       ++dropped;
                                                   }
                                                   else
                                                   {
                                                       co_yield std::move(value);
                                                   }
                                               }); });
    }

    template <typename T>
    auto take_while(std::function<bool(const T &)> &&predicate)
    {
        return transform<T>([predicate = std::move(predicate)](async_generator<T> gen) -> async_generator<T>
                            { for_each_async(value, gen,
                                             {
                                                 if (predicate(value))
                                                 {
                                                     co_yield std::move(value);
                                                 }
                                                 else
                                                 {
                                                     co_return;
                                                 }
                                             }); });
    }

    template <typename T>
    auto drop_while(std::function<bool(const T &)> &&predicate)
    {
        return transform<T>([predicate = std::move(predicate)](async_generator<T> gen) -> async_generator<T>
                            {
                                bool should_drop = true;
                                for_each_async(value, gen,
                                               {
                                                   if (should_drop && predicate(value))
                                                   {
                                                       // Continue dropping values while the predicate is true
                                                   }
                                                   else
                                                   {
                                                       should_drop = false;
                                                       co_yield std::move(value);
                                                   }
                                               }); });
    }

}