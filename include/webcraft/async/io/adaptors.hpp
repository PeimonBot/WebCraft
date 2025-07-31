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
            return adaptor(std::move(stream));
        }

        friend auto operator|(async_readable_stream<T> auto &&stream, Derived &&adaptor)
        {
            return std::move(adaptor)(std::forward<decltype(stream)>(stream));
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

}