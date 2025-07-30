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
    };

    namespace detail
    {

    }
}