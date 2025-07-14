#pragma once

#include <ranges>

namespace webcraft::ranges
{

    template <typename T>
    class to : public std::ranges::range_adaptor_closure<to<T>>
    {
    public:
        constexpr T operator()(std::ranges::input_range auto &&range) const
        {
            T result;

            for (auto &&item : range)
            {
                result.insert(result.end(), std::forward<decltype(item)>(item));
            }

            return std::move(result);
        }
    };
}