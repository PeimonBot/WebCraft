# Asynchronous I/O Implemented in WebCraft


Async I/O in webcraft is implemented using the concept of Streams (similar to Java Streams API and Java Input and Output Streams).
There are 2 types of async streams defined in here:

### Async readable streams
```cpp
template <typename Derived, typename R>
concept async_readable_stream = std::is_move_constructible_v<Derived> && requires(Derived &stream) {
    { stream.recv() } -> std::same_as<task<std::optional<R>>>;
};
```

### Async writable streams
```cpp


```