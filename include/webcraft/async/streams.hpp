#pragma once

#include <ranges>
#include <stdexec/execution.hpp>
#include <exec/task.hpp>
#include <utility>
#include <exec/sequence_senders.hpp>

namespace webcraft::async::io
{

    template <typename T, class StreamOf>
    concept async_readable_stream = requires(T stream) {
        { stream.recv() } -> stdexec::sender;
    } && stdexec::sender_of<decltype(std::declval<T>().recv()), StreamOf>;

    template <typename T, class StreamOf>
    concept async_writable_stream = requires(T stream, StreamOf data) {
        { stream.send(data) } -> stdexec::sender;
    };

    template <class StreamOf, size_t N>
    exec::task<std::span<StreamOf>> read(async_readable_stream<StreamOf> auto &stream, std::span<StreamOf> buffer)
    {
        for (size_t i = 0; i < N; i++)
        {
            auto result = co_await stdexec::stopped_as_optional(stream.recv());
            if (!result)
            {
                co_return buffer.subspan(0, N);
            }
            buffer[i] = *result;
        }
        co_return buffer;
    }

    template <class StreamOf>
    exec::task<void> send(async_writable_stream<StreamOf> auto &stream, std::span<StreamOf> buffer)
    {
        constexpr size_t size = buffer.size();

        for (auto i : std::views::iota(0, size))
        {
            co_await stream.send(buffer[i]);
        }
    }

}