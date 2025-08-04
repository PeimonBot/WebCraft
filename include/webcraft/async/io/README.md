# Asynchronous I/O Implemented in WebCraft

This readme will go over how WebCraft handles **Asynchronous I/O** powered with the latest C++ coroutine features.

## Async Streams

Async I/O in webcraft is implemented using the concept of Streams (similar to Java Streams API and Java Input and Output Streams).
There are 2 types of async streams defined in here: the async readable stream and async writable stream.

### Async readable streams

The definition of it is shown below:
```cpp
template <typename Derived, typename R>
concept async_readable_stream = std::is_move_constructible_v<Derived> && requires(Derived &stream) {
    { stream.recv() } -> std::same_as<task<std::optional<R>>>;
};
```

The any type which models `async_readable_stream<R>` must have a function `recv()` which takes in no arguments and returns a `task<std::optional<R>>`.
Here `R` represents the type of value streamed to the client. The end of the stream is denoted when the result of the task is a `std::nullopt` which subsequent calls to `recv()` will result in.

There is also a buffered variant as shown below for streams which implement buffering internally:
```cpp
template <typename Derived, typename R>
concept async_buffered_readable_stream = async_readable_stream<Derived, R> && requires(Derived &stream, std::span<R> buffer) {
    { stream.recv(buffer) } -> std::same_as<task<std::size_t>>;
};
```
The buffered variant allows you to send in multiple objects at once into the stream. Internally, the regular `recv()` variant will either call the buffered variant with size of 1 or it will internally buffer it so if the values already exist in the queue, it only needs to pop it from there instead of waiting.

### Async writable streams

The definition of it is shown below:
```cpp
template <typename Derived, typename R>
concept async_writable_stream = std::is_move_constructible_v<Derived> && requires(Derived &stream, R &&value) {
    { stream.send(std::forward<R>(value)) } -> std::same_as<task<bool>>;
};
```
Any type which models `async_writable_stream<R>` must have a function `send(R&& )` which takes in an lvalue for R (to be moved into the stream) and returns a `task<bool>` which indicates that the value has been written or not.

There is also a buffered variant which allows you to write multiple objects at once (better for batching).
```cpp
template <typename Derived, typename R>
concept async_buffered_writable_stream = async_writable_stream<Derived, R> && requires(Derived &stream, std::span<R> buffer) {
    { stream.send(buffer) } -> std::same_as<task<size_t>>;
};
```
This version accepts a span of values which will be written to the stream.

### Async stream helpers

For those who do want to read and write multiple values without knowing whether the implementation provides buffering or not, we've created some helper functions:

```cpp
template <typename R, async_readable_stream<R> RStream, size_t BufferSize>
task<std::size_t> recv(RStream &stream, std::span<R, BufferSize> buffer);

template <typename R, async_writable_stream<R> WStream, size_t BufferSize>
task<size_t> send(WStream &stream, std::span<R, BufferSize> buffer);

template <typename R, async_readable_stream<R> RStream>
async_generator<R> to_async_generator(RStream &&stream);

template <typename R>
async_readable_stream<R> auto to_readable_stream(async_generator<R> &&gen);
```

The batched `recv()` and `send()` call the buffered streams variants of `recv()` and `send()` if a buffered stream is passed, otherwise it will call enough `recv()` and `send()` from the non buffered variants until the span is filled or until we can't read or write anymore from the stream.

The conversion to and from async generators are added for readable streams since it would be really nice to be able to do something like this:
```cpp
auto fn = [](size_t limit) -> async_generator<int> {
    size_t count = 0;
    while (count < limit) {
        co_await sleep(200ms);
        co_yield count++;
    }
};

auto rstream = to_readable_stream(fn(10));
while (auto opt = co_await rstream.next()) {
    handle(opt.value());
}
```

This will allow us to add a powerful set of adaptors onto async streams similar to the adaptors added with the ranges library onto iterables. From this, we can build powerful stream processing and pub/sub systems which can do a variety of processing without ever requiring us to have to manually process the stream ourselves

