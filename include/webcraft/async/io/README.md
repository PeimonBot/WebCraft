# Asynchronous I/O in WebCraft

This readme covers how WebCraft handles **Asynchronous I/O** powered with C++ coroutines.

Table of Contents:

1. [Async Streams](#async-streams)
2. [Async Readable Stream Adaptors](#async-readable-stream-adaptors)
3. [Async File I/O](#async-file-io)
4. [Async Socket I/O](#async-socket-io)

## Async Streams

Async I/O in webcraft is implemented using the concept of Streams (similar to Java Streams API and Input/Output Streams).
There are 2 types of async streams defined in the `webcraft::async::io` namespace: async readable streams and async writable streams.

### Async readable streams

The definition is shown below:

```cpp
template <typename Derived, typename R>
concept async_readable_stream = std::is_move_constructible_v<Derived> && requires(Derived &stream) {
    { stream.recv() } -> std::same_as<webcraft::async::task<std::optional<R>>>;
};
```

Any type which models `async_readable_stream<Derived, R>` must have a function `recv()` which takes no arguments and returns a `webcraft::async::task<std::optional<R>>`.
Here `R` represents the type of value streamed to the client. The end of the stream is denoted when the result of the task is `std::nullopt`; subsequent calls to `recv()` will also result in `std::nullopt`.

There is also a buffered variant for streams which implement buffering internally:

```cpp
template <typename Derived, typename R>
concept async_buffered_readable_stream = async_readable_stream<Derived, R> && requires(Derived &stream, std::span<R> buffer) {
    { stream.recv(buffer) } -> std::same_as<webcraft::async::task<std::size_t>>;
};
```

The buffered variant allows you to receive multiple objects at once from the stream. Internally, the regular `recv()` variant will either call the buffered variant with a buffer size of 1 or buffer values internally, so if values already exist in the internal queue, it only needs to pop them instead of waiting.

### Async writable streams

The definition is shown below:

```cpp
template <typename Derived, typename R>
concept async_writable_stream = std::is_move_constructible_v<Derived> && requires(Derived &stream, R value) {
    { stream.send(value) } -> std::same_as<webcraft::async::task<bool>>;
};
```

Any type which models `async_writable_stream<Derived, R>` must have a function `send(R)` which takes an rvalue reference for R (to be moved into the stream) and returns a `webcraft::async::task<bool>` indicating whether the value was successfully written.

There is also a buffered variant which allows you to write multiple objects at once (better for batching):

```cpp
template <typename Derived, typename R>
concept async_buffered_writable_stream = async_writable_stream<Derived, R> && requires(Derived &stream, std::span<R> buffer) {
    { stream.send(buffer) } -> std::same_as<webcraft::async::task<size_t>>;
};
```

This version accepts a span of values which will be written to the stream and returns the number of values actually written.

### Async stream helpers

For those who want to read and write multiple values without knowing whether the implementation provides buffering, we provide helper functions in the `webcraft::async::io` namespace:

```cpp
template <typename R, async_readable_stream<R> RStream, size_t BufferSize>
webcraft::async::task<std::size_t> recv(RStream &stream, std::span<R, BufferSize> buffer);

template <typename R, async_writable_stream<R> WStream, size_t BufferSize>
webcraft::async::task<size_t> send(WStream &stream, std::span<R, BufferSize> buffer);

template <typename R, async_readable_stream<R> RStream>
cppcoro::async_generator<R> to_async_generator(RStream &&stream);

template <typename R>
async_readable_stream<R> auto to_readable_stream(cppcoro::async_generator<R> &&gen);
```

The batched `recv()` and `send()` functions call the buffered stream variants if a buffered stream is passed, otherwise they call the non-buffered variants repeatedly until the span is filled or until no more data can be read/written.

The conversion to and from async generators are provided for readable streams since it enables powerful functional-style stream processing:

```cpp
auto fn = [](size_t limit) -> async_generator<int> {
    size_t count = 0;
    while (count < limit) {
        co_await sleep(200ms);
        co_yield count++;
    }
};

auto rstream = webcraft::async::io::to_readable_stream(fn(10));
while (auto opt = co_await rstream.recv()) {
    handle(opt.value());
}
```

This will allow us to add a powerful set of adaptors onto async streams similar to the adaptors added with the ranges library onto iterables. From this, we can build powerful stream processing and pub/sub systems which can do a variety of processing without requiring manual stream processing.

**Note:** All examples in this document use the `webcraft::async::io` namespace. For brevity, some code examples may omit the full namespace qualification, but all types and functions are within this namespace.

### Channels

Channels are a mechanism to transfer data from a publisher to a subscriber. The model implemented is MPSC (Multiple Producer, Single Consumer) since it only makes sense to deal with one event at a time.
You can create an MPSC channel as shown below:

```cpp
auto [rstream, wstream] = webcraft::async::io::make_mpsc_channel<int>();
```

The type of `rstream` satisfies `async_readable_stream` and the type of `wstream` satisfies `async_writable_stream`. This effectively creates an asynchronous pipe. Concurrency is handled automatically - whenever `send()` is called on the writable stream, any waiting read operations are resumed.

**NOTE: DO NOT pipe `rstream` into `wstream` as it will cause an infinite loop since all values received from read will be sent to write, which will trigger another read, creating a stack overflow.**

Working with channels becomes useful for building highly scalable Publisher-Subscriber applications. Most microservices use message queues which internally use channels since they make working with event streams much easier.
Channels are used internally for managing async socket I/O and async file I/O.

## Async Readable Stream Adaptors

Streams aren't really useful by themselves. Most of the time, we want to transform raw data into something useful. This is the idea of a **stream adaptor**. We take a readable stream of one data type, apply some operation (mapping, filtering, transforming), and get a stream of another data type.

Here's an example:

Suppose we want to group students into a map by letter grade (A for 80-100, B for 70-80, C for 60-70, D for 50-60), filter out failing students, and sort students within each group:

```cpp
struct student {
    std::string name;
    double average;
};
```

The adaptor-based solution using WebCraft's stream adaptors:

```cpp
std::string average_to_grade(double average) {
    if (average >= 80.0) return "A";
    if (average >= 70.0) return "B";
    if (average >= 60.0) return "C";
    return "D";
}

webcraft::async::task<std::unordered_map<std::string, std::vector<student>>> 
get_student_grade_groupings(webcraft::async::io::async_readable_stream<student> auto students) {
    using namespace webcraft::async::io::adaptors;
    
    co_return co_await (std::move(students) 
        | filter([](const auto& st) { return st.average >= 50.0; })
        | collect(collectors::group_by([](const student& st) { 
            return average_to_grade(st.average); 
          })));
}
```

This greatly reduces the amount of code and logic required. There are many other uses for async streams, especially when dealing with pub/sub systems.

### Stream Adaptors Implementation

All stream adaptors inherit from `async_readable_stream_adaptor`. The definition is shown below:

```cpp
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
```

#### Transform adaptor

Definition:

```cpp
template <typename InType, typename Func>
auto transform(Func &&fn) -> /* adaptor type */;
```

Transform an existing async_readable_stream to another async_readable_stream. The function must have signature `cppcoro::async_generator<OutType>(cppcoro::async_generator<InType>)`. Example:

```cpp
mock_readable_stream stream({1,2,3,4,5});

auto new_stream = stream | webcraft::async::io::adaptors::transform<int>([](cppcoro::async_generator<int> gen) -> cppcoro::async_generator<std::string> {
    for_each_async(value, gen, {
        co_yield std::to_string(value);
        co_yield std::to_string(value * 2); // Duplicate and double
    });
});
// Result: ["1", "2", "2", "4", "3", "6", "4", "8", "5", "10"]
```

#### Map adaptor

Definition:

```cpp
template <typename InType, typename Func, typename OutType = std::invoke_result_t<Func, InType>>
auto map(Func &&fn) -> /* adaptor type */;
```

Create a new readable stream with values mapped using the provided function. Example:

```cpp
mock_readable_stream stream({1,2,3,4,5});

auto new_stream = stream | webcraft::async::io::adaptors::map<int>([](int value) {
    return std::to_string(value);
});
// Result: ["1", "2", "3", "4", "5"]
```

#### Pipe adaptor

Definition:

```cpp
template <typename T>
    requires std::is_copy_assignable_v<T>
auto pipe(webcraft::async::io::async_writable_stream<T> auto &str) -> /* adaptor type */;
```

Create a new readable stream that forwards read values to the provided writable stream. Example:

```cpp
mock_readable_stream<int> rstream({1,2,3,4,5});
mock_writable_stream<int> wstream;

auto new_stream = rstream | webcraft::async::io::adaptors::pipe(wstream);

while (auto opt = co_await new_stream.recv()) {
    assert(wstream.received_value(*opt));
}
```

#### Filter adaptor

Definition:

```cpp
template <typename T, typename Func>
auto filter(Func &&predicate) -> /* adaptor type */;
```

Filter values in the stream based on a predicate. Example:

```cpp
mock_readable_stream<int> stream({1,2,3,4,5});
auto new_stream = stream | webcraft::async::io::adaptors::filter<int>([](int value) { 
    return value % 2 == 0; 
});
// Result: [2,4]
```

#### Limit adaptor

Definition:

```cpp
template <typename T>
auto limit(size_t size) -> /* adaptor type */;
```

Limit the number of values sent through the stream. Example:

```cpp
mock_readable_stream<int> stream({1,2,3,4,5});
auto new_stream = stream | webcraft::async::io::adaptors::limit<int>(3);
// Result: [1,2,3]
```

#### Skip adaptor

Definition:

```cpp
template <typename T>
auto skip(size_t size) -> /* adaptor type */;
```

Skip a number of values at the beginning of the stream. Example:

```cpp
mock_readable_stream<int> stream({1,2,3,4,5});
auto new_stream = stream | webcraft::async::io::adaptors::skip<int>(2);
// Result: [3,4,5]
```

#### Take while adaptor

Definition is shown below:

```cpp
template <typename T, typename Func>
auto take_while(Func&& func) -> std::is_derived_from<async_readable_stream_adaptor>;
```

Using this adaptor, you'd be able to take the values sent through the stream while the predicate defined by `Func` yields true. `Func` must have the following type signature `bool(const T&)`. An example is shown below:

```cpp
mock_readable_stream<int> stream({1,2,3,4,5});
async_readable_stream<int> auto new_stream = stream | take_while([](int i) { return i < 2; });
// streams returned is [1,2]
```

#### Drop while adaptor

Definition is shown below:

```cpp
template <typename T, typename Func>
auto drop_while(Func&& func) -> std::is_derived_from<async_readable_stream_adaptor>;
```

Using this adaptor, you'd be able to drop the values sent through the stream while the predicate defined by `Func` yields true. `Func` must have the following type signature `bool(const T&)`. An example is shown below:

```cpp
mock_readable_stream<int> stream({1,2,3,4,5});
async_readable_stream<int> auto new_stream = stream | drop_while([](int i) { return i < 2; });
// streams returned is [3,4,5]
```

#### Collect adaptor

Definition is shown below:

```cpp
template <typename Derived, typename ToType, typename StreamType>
concept collector = std::is_invocable_r_v<task<ToType>, Derived, async_generator<StreamType>>;

template <typename ToType, typename StreamType, collector<ToType, StreamType> Collector>
auto collect(Collector &&collector_func) -> std::is_derived_from<async_readable_stream_adaptor>;
```

Using this adaptor, you'll be able to convert the readable stream into something more tangible like another object or a `std::vector` of an object. This would be really useful especially for processing of REST requests when they give you a stream of bytes, you'll be able to collect it into a JSON object.

There are many ways to create your own collector, you can create your own, for example to convert a stream of bytes into a JSON object, or you can use some of the in-built ones:

- reduce
- joining
- to_vector
- group_by

##### Reduce collector

Defined as shown:

```cpp
template <typename T>
auto reduce(std::function<T(T, T)> &&func);
```

When collected, this will return a `task<T>` which can be awaited to give you the result. For example:

```cpp
mock_readable_stream<int> stream({1,2,3,4,5});
int values = co_await (stream | collect(collectors::reduce([](int first, int second) { return first + second; })));
assert(values == 15);
```

##### Joining collector

Defined as shown:

```cpp
template <typename T>
    requires std::is_convertible_v<T, std::string>
auto joining(std::string separator = "", std::string prefix = "", std::string suffix = "")
```

When collected, this will return a `task<std::string>` which can be awaited to give you the result. For example:

```cpp
mock_readable_stream<int> stream({"1","2","3","4","5"});
std::string values = co_await (stream | collect(collectors::joinin(",")));
assert(values == "1,2,3,4,5");
```

##### To Vector collector

Defined as shown:

```cpp
template <typename T>
auto to_vector();
```

When collected, this will return a `task<std::vector<T>>` which can be awaited to give you the result. For example:

```cpp
mock_readable_stream<int> stream({1,2,3,4,5});
std::vector<int> values = co_await (stream | collect(collectors::to_vector()));
for (int i = 0; i < values.size(); i++) {
    assert(values[i] == i + 1);
}
```

##### Group By collector

Defined as shown:

```cpp
template <typename T, typename KeyType>
auto group_by(std::function<KeyType(const T &)> key_function);
```

When collected, this will return a `task<std::unordered_map<KeyType, std::vector<T>>>` which can be awaited to give you the result. Example:

```cpp
mock_readable_stream<std::string> stream({"AB", "BC", "AC", "BD", "CD"});
auto mapper = [](std::string value) -> std::string { // groups it by the first letter
    return std::string(value[0]); // A* -> A, B* -> B
}

std::unordered_map<std::string, std::vector<std::string>> map = co_await (stream | collect(collectors::group_by(mapper)));
//  returns a { {"A", ["AB","AC"]}, {"B", ["BC","BD"]}, {"C", ["CD"]} }
```

#### Forward To adaptor

Definition is shown below:

```cpp
template <typename T>
auto forward_to(async_writable_stream<T> auto &stream) -> std::is_derived_from<async_readable_stream_adaptor>;
```

Using this adaptor, you'll be able to forward all objects coming from the readable stream into the output stream passed. When applied, the adaptor will return a `task<void>` which you can await for all the values to be sent into the stream. An example of this is shown below:

```cpp
mock_readable_stream<int> rstream({1,2,3,4,5});
mock_writable_stream<int> wstream;

co_await (rstream | forward_to(wstream));
for (int i = 1; i <= 5; i++) {
    assert(wstream.received(i));
}
```

#### Min adaptor

Definition is shown below:

```cpp
template <typename T>
    requires std::totally_ordered<T>
auto min() -> std::is_derived_from<async_readable_stream_adaptor>;
```

Using this adaptor, you'd be able to find the minimum value sent through the stream. An example is shown below:

```cpp
mock_readable_stream<int> stream({1,2,3,4,5});
int min_value = co_await (stream | min());
assert(min_value == 1);
```

#### Max adaptor

Definition is shown below:

```cpp
template <typename T>
    requires std::totally_ordered<T>
auto max() -> std::is_derived_from<async_readable_stream_adaptor>;
```

Using this adaptor, you'd be able to find the maximum value sent through the stream. An example is shown below:

```cpp
mock_readable_stream<int> stream({1,2,3,4,5});
int max_value = co_await (stream | max());
assert(max_value == 5);
```

#### Sum adaptor

Definition is shown below:

```cpp
template <typename T>
concept closure_under_addition = requires(T a, T b) {
    { a + b } -> std::convertible_to<T>;
};

template <typename T>
    requires closure_under_addition<T>
auto sum() -> std::is_derived_from<async_readable_stream_adaptor>;
```

Using this adaptor, you'd be able to find the sum of the values sent through the stream. An example is shown below:

```cpp
mock_readable_stream<int> stream({1,2,3,4,5});
int sum = co_await (stream | sum());
assert(sum == 15);
```

#### Find first adaptor

Definition is shown below:

```cpp
template <typename T, typename Func>
auto find_first(Func&& func) -> std::is_derived_from<async_readable_stream_adaptor>;
```

Using this adaptor, you'd be able to find the first value sent through the stream which matches the following predicate. An example is shown below:

```cpp
mock_readable_stream<int> stream({1,2,3,4,5});
int value = co_await (stream | find_first([](int value) { return value % 2 == 0; }));
assert(value == 2);
```

#### Find last adaptor

Definition is shown below:

```cpp
template <typename T, typename Func>
auto find_last(Func&& func) -> std::is_derived_from<async_readable_stream_adaptor>;
```

Using this adaptor, you'd be able to find the last value sent through the stream which matches the following predicate. An example is shown below:

```cpp
mock_readable_stream<int> stream({1,2,3,4,5});
int value = co_await (stream | find_last([](int value) { return value % 2 == 0; }));
assert(value == 4);
```

#### Any matches adaptor

Definition is shown below:

```cpp
template <typename T, typename Func>
auto any_matches(Func&& func) -> std::is_derived_from<async_readable_stream_adaptor>;
```

Using this adaptor, you'd be able to check if there are any values which match the predicate. An example is shown below:

```cpp
mock_readable_stream<int> stream_1({2,4,3,5});
mock_readable_stream<int> stream_2({1,3,5});
bool check1 = co_await (stream_1 | any_matches([](int value) { return value % 2 == 0; }));
assert(check1);

bool check2 = co_await (stream_2 | any_matches([](int value) { return value % 2 == 0; }));
assert(!check2);
```

#### All matches adaptor

Definition is shown below:

```cpp
template <typename T, typename Func>
auto all_matches(Func&& func) -> std::is_derived_from<async_readable_stream_adaptor>;
```

Using this adaptor, you'd be able to check if all values which match the predicate. An example is shown below:

```cpp
mock_readable_stream<int> stream_1({2,4,6,8});
mock_readable_stream<int> stream_2({2,4,6,7});
bool check1 = co_await (stream_1 | all_matches([](int value) { return value % 2 == 0; }));
assert(check1);

bool check2 = co_await (stream_2 | all_matches([](int value) { return value % 2 == 0; }));
assert(!check2);
```

#### None matches adaptor

Definition is shown below:

```cpp
template <typename T, typename Func>
auto any_matches(Func&& func) -> std::is_derived_from<async_readable_stream_adaptor>;
```

Using this adaptor, you'd be able to check if all values do not match the predicate. An example is shown below:

```cpp
mock_readable_stream<int> stream_1({2,4,3,5});
mock_readable_stream<int> stream_2({1,3,5});
bool check1 = co_await (stream_1 | none_matches([](int value) { return value % 2 == 0; }));
assert(!check1);

bool check2 = co_await (stream_2 | none_matches([](int value) { return value % 2 == 0; }));
assert(check2);
```

### Some of the adaptors are planned to be implemented in this framework:

#### Sorted adaptor

Definition is as shown below:

```cpp
template<typename T>
requires std::totally_ordered<T>
auto sorted() -> std::is_derived_from<async_readable_stream_adaptor>;

template<typename T, typename R>
requires std::totally_ordered<R>
auto sorted(std::function<R(T)> comparator) -> std::is_derived_from<async_readable_stream_adaptor>;
```

Using this adaptor on a readable stream will create a new sorted readable stream from the old stream (all values will be sorted in the order specified). An example of the usage is shown below:

```cpp
mock_readable_stream<int> values_1({5,1,3,6,4,2});
async_readable_stream<int> auto new_stream_1 = values_1 | sorted(); // 1,2,3,4,5,6
// the new steam satisfies async_readable_stream<1>

// another example
mock_readable_stream<std::pair<int, std::string>> values_2({{5,"5"}, {1,"1"}, {"3",3}, {6,"6"}, {4,"4"},{2,"2"}});
async_readable_stream<std::pair<int, std::string>> auto new_stream_2 = values_2 | sorted([](auto value) { return value.key; }); // {1,"1"},{2,"2"},{3,"3"},{4,"4"},{5,"5"},{6,"6"}
```

#### Zip adaptor

```cpp
template<typename T, typename R>
auto zip_inner(async_readable_stream<R> str) -> std::is_derived_from<async_readable_stream_adaptor>;

template<typename T, typename R>
auto zip_left(async_readable_stream<R> str) -> std::is_derived_from<async_readable_stream_adaptor>;

template<typename T, typename R>
auto zip_right(async_readable_stream<R> str) -> std::is_derived_from<async_readable_stream_adaptor>;

template<typename T, typename R>
auto zip_full(async_readable_stream<R> str) -> std::is_derived_from<async_readable_stream_adaptor>;
```

Using this adaptor you'll be able to group 2 streams together into one. An example of this is shown below:

```cpp
mock_readable_stream<int> stream1({1,2,3,4,5});
mock_readable_stream<std::string> stream2({"1","2","3","4","5"});

async_readable_stream<std::pair<std::optional<int>, std::optional<std::string>>> auto new_stream = stream1 | zip_full(stream2); // {1,"1"},{2,"2"},{3,"3"},{4,"4"},{5,"5"},{6,"6"}
// the new steam satisfies async_readable_stream<std::pair<std::optional<int>, std::optional<std::string>>>
```

## Async File I/O

Async File I/O is handled differently on different platforms using the `webcraft::async::io::fs` namespace. The framework provides a unified interface while leveraging platform-specific optimizations:

### File Operations Table


| Library | Platforms Supported | Special Create? | Async Read? | Async Write? | Async Close? | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| IO Completion Ports | Windows Only (`<windows.h>`) | Synchronous but sets up async IO:[`CreateFileEx`](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilea) + [`CreateIOCompletionPort`](https://learn.microsoft.com/en-us/windows/win32/fileio/createiocompletionport) | [`ReadFile`](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-readfile) | [`WriteFile`](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-writefile) | No Async Version. Just [`CloseHandle`](https://learn.microsoft.com/en-us/windows/win32/api/handleapi/nf-handleapi-closehandle) | **Summary:** Synchronous create and close and async read and write but only for windows. |
| io_uring | Linux Only (`<liburing.h>`) | [`io_uring_prep_open`](https://man7.org/linux/man-pages/man3/io_uring_prep_open.3.html) | [`io_uring_prep_read`](https://man7.org/linux/man-pages/man3/io_uring_prep_read.3.html) | [`io_uring_prep_write`](https://man7.org/linux/man-pages/man3/io_uring_prep_write.3.html) | [`io_uring_prep_close`](https://man7.org/linux/man-pages/man3/io_uring_prep_close.3.html) | **Summary:** Has async support for all file functions but only for linux |
| kqueue + aio | Pure BSD-based systems like FreeBSD (`<sys/event.h>` + `<aio.h>`) | Synchronous: Use POSIX`open` | Use`aio_read` with kqueue | Use`aio_write` with | Synchronous: Use `close` | **NOTE: Make sure when the kqueue result has returned to call `aio_return`.** |
| Thread pool | MacOS or any other system which does not support Async File I/O natively | Synchronous: Use POSIX`open` | Use`read` on thread pool | Use`write` on thread | Synchronous: Use`close` | Use a thread pool |
| GCD | MacOS Only - plan on implementing this in the next PR | tbd | tdb | tdb | tdb | Need to look into this more |

## Async Socket I/O

Async Socket I/O is handled differently on different platforms using the `webcraft::async::io::socket` namespace.

### TCP Sockets Table


| Library  | Platforms Supported | Special Create? | Async Connect? | Async Read? | Async Write? | Async Close? | Async Shutdown? | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| io_uring | Linux | [`io_uring_prep_socket`](https://man7.org/linux/man-pages/man3/io_uring_prep_socket.3.html) | [`io_uring_prep_connect`](https://man7.org/linux/man-pages/man3/io_uring_prep_connect.3.html) | [`io_uring_prep_read`](https://man7.org/linux/man-pages/man3/io_uring_prep_read.3.html) | [`io_uring_prep_write`](https://man7.org/linux/man-pages/man3/io_uring_prep_write.3.html) | [`io_uring_prep_close`](https://man7.org/linux/man-pages/man3/io_uring_prep_close.3.html) | [`io_uring_prep_shutdown`](https://man7.org/linux/man-pages/man3/io_uring_prep_shutdown.3.html) | **NOTE: All the functions are async just linux only.** |
| IOCP | Windows | [`socket`](https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-socket) + [`CreateIOCompletionPort`](https://learn.microsoft.com/en-us/windows/win32/fileio/createiocompletionport) | [`ConnectEx`](https://learn.microsoft.com/en-us/windows/win32/api/mswsock/nc-mswsock-lpfn_connectex) | [`WSARecv`](https://learn.microsoft.com/en-gb/windows/win32/api/winsock2/nf-winsock2-wsarecv) | [`WSASend`](https://learn.microsoft.com/en-gb/windows/win32/api/winsock2/nf-winsock2-wsasend) | [`closesocket`](https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-closesocket) | [`shutdown`](https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-shutdown) | **NOTE: A call to `bind()` must be made before calling ConnectEx otherwise socket will be invalid.** |
| kqueue | BSD based systems | `socket` | `connect` | `read` | `write` | `close` | `shutdown` | **NOTE: It seems like it's all just synchronous API calls but kqueue lets us know when to call what under the hood. Kqueue will be our notification agent and we'll use channels and other forms of asynchronous delivery to let us know when to resume. Refer to the kqueue example below.** |


Some docs: 
- https://learn.microsoft.com/en-us/windows/win32/api/mswsock/nc-mswsock-lpfn_connectex
- https://gist.github.com/joeyadams/4158972
- https://stackoverflow.com/questions/13598530/connectex-requires-the-socket-to-be-initially-bound-but-to-what

### TCP Listeners Table


| Library  | Platforms Supported | Special Create? | Async Bind? | Async Listen? | Async Accept | Async Close? | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- |
| io_uring | Linux | [`io_uring_prep_socket`](https://man7.org/linux/man-pages/man3/io_uring_prep_socket.3.html) | [`io_uring_prep_bind`](https://man7.org/linux/man-pages/man3/io_uring_prep_bind.3.html)| [`io_uring_prep_listen`](https://man7.org/linux/man-pages/man3/io_uring_prep_listen.3.html) | [`io_uring_prep_accept`](https://man7.org/linux/man-pages/man3/io_uring_prep_accept.3.html) | [`io_uring_prep_close`](https://man7.org/linux/man-pages/man3/io_uring_prep_close.3.html) | **NOTE: All the functions are async just linux only.** |
| IOCP | Windows | [`socket`](https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-socket) + [`CreateIOCompletionPort`](https://learn.microsoft.com/en-us/windows/win32/fileio/createiocompletionport) | [`bind`](https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-bind) | [`listen`](https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-listen) | [`AcceptEx`](https://learn.microsoft.com/en-gb/windows/win32/api/mswsock/nf-mswsock-acceptex) | [`closesocket`](https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-closesocket) | |
| kqueue | BSD based systems | `socket` | `bind` | `listen` | `accept` | `close` | **NOTE: It seems like it's all just synchronous API calls but kqueue lets us know when to call what under the hood. Kqueue will be our notification agent and we'll use channels and other forms of asynchronous delivery to let us know when to resume. Refer to the kqueue example below.** |

Examples:
- https://learn.microsoft.com/en-gb/windows/win32/api/mswsock/nf-mswsock-acceptex#example-code
- https://gist.github.com/josephg/6c078a241b0e9e538ac04ef28be6e787
- KQUEUE Example: https://dev.to/frevib/a-tcp-server-with-kqueue-527

### UDP Datagram Sockets

Datagram sockets are different from traditional I/O models. Unlike TCP sockets which maintain a persistent connection between a client socket and a server socket, datagram send and receive data packets to and from other datagram sockets. They are also unreliable but fast.

| Library  | Platforms Supported | Special Create? | Async Bind? | Async Receive From? | Async Send to? | Async Close? | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- |
| io_uring | Linux | [`io_uring_prep_socket`](https://man7.org/linux/man-pages/man3/io_uring_prep_socket.3.html) | [`io_uring_prep_bind`](https://man7.org/linux/man-pages/man3/io_uring_prep_bind.3.html)| [`io_uring_prep_recvmsg`](https://man7.org/linux/man-pages/man3/io_uring_prep_recvmsg.3.html) | [`io_uring_prep_sendmsg`](https://man7.org/linux/man-pages/man3/io_uring_prep_sendmsg.3.html) | [`io_uring_prep_close`](https://man7.org/linux/man-pages/man3/io_uring_prep_close.3.html) | **NOTE: All the functions are async just linux only.** |
| IOCP | Windows | [`socket`](https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-socket) + [`CreateIOCompletionPort`](https://learn.microsoft.com/en-us/windows/win32/fileio/createiocompletionport) | [`bind`](https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-bind) | [`WSARecvFrom`](https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-wsarecvfrom) | [`WSASendTo`](https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-wsasendto) | [`closesocket`](https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-closesocket) | |
| kqueue | BSD based systems | `socket` | `bind` | `listen` | `accept` | `close` | **NOTE: It seems like it's all just synchronous API calls but kqueue lets us know when to call what under the hood. Kqueue will be our notification agent and we'll use channels and other forms of asynchronous delivery to let us know when to resume. Refer to the kqueue example below.** |

## Planned implementation:

Async Read & Async Write will be based on what's in the **Async Read** and **Async Write** columns. Closing will still happen with RAII (which would be a synchronous close on everything but linux) but on linux we'll send a fire-and-forget request with `io_uring_prep_close`. For async creation it can either be with synchronous or asynchronous, the bottleneck wouldn't be too big of an issue. 

This is the following specification for using the **Async File I/O** API:
```cpp
namespace detail
{

    class file_descriptor
    {
    protected:
        std::ios_base::openmode mode;

    public:
        file_descriptor(std::ios_base::openmode mode) : mode(mode) {}
        virtual ~file_descriptor() = default;

        // virtual because we want to allow platform specific implementation
        virtual task<size_t> read(std::span<char> buffer) = 0;  // internally should check if openmode is for read
        virtual task<size_t> write(std::span<char> buffer) = 0; // internally should check if openmode is for write or append
        virtual task<void> close() = 0;                         // will spawn a fire and forget task (essentially use async apis but provide null callback)
    };

    task<std::shared_ptr<file_descriptor>> make_file_descriptor(std::filesystem::path p, std::ios_base::openmode mode);

    class file_stream
    {
    protected:
        std::shared_ptr<file_descriptor> fd;
        std::atomic<bool> closed{false};

    public:
        explicit file_stream(std::shared_ptr<file_descriptor> fd) : fd(std::move(fd)) {}
        virtual ~file_stream() noexcept
        {
            if (fd)
                sync_wait(close());
        }

        task<void> close() noexcept
        {
            bool expected = false;
            if (closed.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            {
                co_await fd->close();
            }
        }
    };
}

class file_rstream : public detail::file_stream
{
public:
    file_rstream(std::shared_ptr<detail::file_descriptor> fd) : detail::file_stream(std::move(fd)) {}
    ~file_rstream() noexcept = default;

    file_rstream(file_rstream &&) noexcept = default;
    file_rstream &operator=(file_rstream &&) noexcept = default;

    task<size_t> recv(std::span<char> buffer)
    {
        return fd->read(buffer);
    }

    task<char> recv()
    {
        std::array<char, 1> buf;
        co_await recv(buf);
        co_return buf[0];
    }
};

static_assert(async_readable_stream<file_rstream, char>);
static_assert(async_buffered_readable_stream<file_rstream, char>);
static_assert(async_closeable_stream<file_rstream, char>);

class file_wstream : public detail::file_stream
{
public:
    explicit file_wstream(std::shared_ptr<file_descriptor> fd) : detail::file_stream(std::move(fd)) {}
    ~file_wstream() noexcept = default;

    file_wstream(file_wstream &&) noexcept = default;
    file_wstream &operator=(file_wstream &&) noexcept = default;

    task<size_t> send(std::span<char> buffer)
    {
        return fd->write(buffer);
    }

    task<bool> send(char b)
    {
        std::array<char, 1> buf;
        buf[0] = b;
        co_await send(buf);
        co_return true;
    }
};

static_assert(async_writable_stream<file_wstream, char>);
static_assert(async_buffered_writable_stream<file_wstream, char>);
static_assert(async_closeable_stream<file_wstream, char>);

class file
{
private:
    std::filesystem::path p;

public:
    file(std::filesystem::path p) : p(std::move(p)) {}
    ~file() = default;

    task<file_rstream> open_readable_stream()
    {
        auto descriptor = co_await detail::make_file_descriptor(p, std::ios_base::in);
        co_return file_rstream(descriptor);
    }

    task<file_wstream> open_writable_stream(bool append)
    {
        auto descriptor = co_await detail::make_file_descriptor(p, append ? std::ios_base::app : std::ios_base::out);
        co_return file_wstream(std::move(descriptor));
    }

    constexpr const std::filesystem::path get_path() const { return p; }
    constexpr operator const std::filesystem::path &() const { return p; }
};

file make_file(std::filesystem::path p)
{
    return file(p);
}
```

This is the following specification for using the **Async TCP Socket and Listener I/O** API:
```cpp
struct connection_info
{
    std::string host;
    uint16_t port;
};

enum class socket_stream_mode
{
    READ,
    WRITE
};

namespace detail
{

    class tcp_descriptor_base
    {
    public:
        tcp_descriptor_base() = default;
        virtual ~tcp_descriptor_base() = default;

        virtual task<void> close() = 0; // Close the socket
    };

    class tcp_socket_descriptor : public tcp_descriptor_base
    {
    public:
        tcp_socket_descriptor() = default;
        virtual ~tcp_socket_descriptor() = default;

        virtual task<void> connect(const connection_info &info) = 0;  // Connect to a server
        virtual task<size_t> read(std::span<char> buffer) = 0;        // Read data from the socket
        virtual task<size_t> write(std::span<const char> buffer) = 0; // Write data to the socket
        virtual void shutdown(socket_stream_mode mode) = 0;     // Shutdown the socket
    };

    class tcp_listener_descriptor : public tcp_descriptor_base
    {
    public:
        tcp_listener_descriptor() = default;
        virtual ~tcp_listener_descriptor() = default;

        virtual void bind(const connection_info &info) = 0;          // Bind the listener to an address
        virtual void listen(int backlog) = 0;                        // Start listening for incoming connections
        virtual task<std::unique_ptr<tcp_socket_descriptor>> accept() = 0; // Accept a new connection
    };

    class udp_socket_descriptor
    {
    public:
        udp_socket_descriptor() = default;
        virtual ~udp_socket_descriptor() = default;

        virtual task<void> close() = 0; // Close the socket
        virtual void bind(const connection_info& info) = 0; // binds a DGRAM socket to a host and port
        virtual task<size_t> recvfrom(std::span<char> buffer, connection_info& from_address) = 0; // receives a packet from some address (which is populated)
        virtual task<size_t> sendto(std::span<const char> buffer, const connection_info& to_address) = 0; // sends a packet to some address (which is provided)
    };

    std::shared_ptr<tcp_socket_descriptor> make_tcp_socket_descriptor();
    std::shared_ptr<tcp_listener_descriptor> make_tcp_listener_descriptor();
    std::shared_ptr<udp_socket_descriptor> make_udp_socket_descriptor();

}

class tcp_rstream
{
private:
    std::shared_ptr<detail::tcp_socket_descriptor> descriptor;

public:
    tcp_rstream(std::shared_ptr<detail::tcp_socket_descriptor> desc) : descriptor(std::move(desc)) {}
    ~tcp_rstream() = default;

    task<size_t> recv(std::span<char> buffer)
    {
        return descriptor->read(buffer);
    }

    task<char> recv()
    {
        std::array<char, 1> buf;
        co_await recv(buf);
        co_return buf[0];
    }

    task<void> close()
    {
        descriptor->shutdown(socket_stream_mode::READ);
        co_return;
    }
};

static_assert(async_writable_stream<tcp_rstream, char>);
static_assert(async_buffered_writable_stream<tcp_rstream, char>);
static_assert(async_closeable_stream<tcp_rstream, char>);

class tcp_wstream
{
private:
    std::shared_ptr<detail::tcp_socket_descriptor> descriptor;

public:
    tcp_wstream(std::shared_ptr<detail::tcp_socket_descriptor> desc) : descriptor(std::move(desc)) {}
    ~tcp_wstream() = default;

    task<size_t> send(std::span<const char> buffer)
    {
        return descriptor->write(buffer);
    }

    task<bool> send(char b)
    {
        std::array<char, 1> buf;
        buf[0] = b;
        co_await send(buf);
        co_return true;
    }

    task<void> close()
    {
        descriptor->shutdown(socket_stream_mode::WRITE);
        co_return;
    }
};

static_assert(async_writable_stream<tcp_wstream, char>);
static_assert(async_buffered_writable_stream<tcp_wstream, char>);
static_assert(async_closeable_stream<tcp_wstream, char>);


class tcp_socket
{
private:
    std::shared_ptr<detail::tcp_socket_descriptor> descriptor;
    tcp_rstream read_stream;
    tcp_wstream write_stream;
    bool read_shutdown{false};
    bool write_shutdown{false};

public:
    tcp_socket(std::shared_ptr<detail::tcp_socket_descriptor> desc) : descriptor(desc), read_stream(descriptor), write_stream(descriptor)
    {
    }

    ~tcp_socket()
    {
        fire_and_forget(close());
    }

    tcp_socket(tcp_socket &&other) noexcept
        : descriptor(std::exchange(other.descriptor, nullptr)),
            read_stream(std::move(other.read_stream)),
            write_stream(std::move(other.write_stream))
    {
    }

    tcp_socket &operator=(tcp_socket &&other) noexcept
    {
        if (this != &other)
        {
            descriptor = std::exchange(other.descriptor, nullptr);
            read_stream = std::move(other.read_stream);
            write_stream = std::move(other.write_stream);
        }
        return *this;
    }

    task<void> connect(const connection_info &info)
    {
        if (!descriptor)
            throw std::runtime_error("Descriptor is null");

        co_await descriptor->connect(info);
    }

    tcp_rstream &get_readable_stream()
    {
        if (!descriptor)
            throw std::runtime_error("Descriptor is null");
        return read_stream;
    }

    tcp_wstream &get_writable_stream()
    {
        if (!descriptor)
            throw std::runtime_error("Descriptor is null");
        return write_stream;
    }

    void shutdown_channel(socket_stream_mode mode)
    {
        if (mode == socket_stream_mode::READ && !read_shutdown)
        {
            descriptor->shutdown(socket_stream_mode::READ);
            this->read_shutdown = true;
        }
        else if (mode == socket_stream_mode::WRITE && !write_shutdown)
        {
            descriptor->shutdown(socket_stream_mode::WRITE);
            this->write_shutdown = true;
        }
    }

    task<void> close()
    {
        if (descriptor)
        {
            shutdown_channel(socket_stream_mode::READ);
            shutdown_channel(socket_stream_mode::WRITE);
            co_await descriptor->close();
            descriptor.reset();
        }
    }

    inline std::string get_remote_host()
    {
        return descriptor->get_remote_host();
    }

    inline uint16_t get_remote_port()
    {
        return descriptor->get_remote_port();
    }
};

class tcp_listener
{
private:
    std::shared_ptr<detail::tcp_listener_descriptor> descriptor;

public:
    tcp_listener(std::shared_ptr<detail::tcp_listener_descriptor> desc) : descriptor(std::move(desc)) {}
    ~tcp_listener()
    {
        if (descriptor)
        {
            fire_and_forget(descriptor->close());
        }
    }

    void bind(const connection_info &info)
    {
        descriptor->bind(info);
    }

    void listen(int backlog)
    {
        descriptor->listen(backlog);
    }

    task<tcp_socket> accept()
    {
        co_return co_await descriptor->accept();
    }
};

class udp_socket
{
private:
    std::shared_ptr<detail::udp_socket_descriptor> descriptor;

public:
    udp_socket(std::shared_ptr<detail::udp_socket_descriptor> desc): descriptor(std::move(desc)) {}
    ~udp_socket()
    {
        if (descriptor)
        {
            fire_and_forget(descriptor->close());
        }
    }

    void bind(const connection_info &info)
    {
        descriptor->bind(info);
    }

    task<size_t> recvfrom(std::span<char> buffer, connection_info& from_address)
    {
        if (!descriptor)
            throw std::runtime_error("Descriptor is null");
        return descriptor->recvfrom(buffer, from_address);
    }
    
    task<size_t> sendto(std::span<const char> buffer, const connection_info& to_address)
    {
        if (!descriptor)
            throw std::runtime_error("Descriptor is null");
        return descriptor->sendto(buffer, to_address);
    }

};


task<tcp_socket> make_tcp_socket()
{
    auto descriptor = co_await detail::make_tcp_socket_descriptor();
    co_return tcp_socket(std::move(descriptor));
}

task<tcp_listener> make_tcp_listener()
{
    auto descriptor = co_await detail::make_tcp_listener_descriptor();
    co_return tcp_listener(std::move(descriptor));
}

task<udp_socket> make_udp_socket()
{
    auto descriptor = co_await detail::make_udp_socket_descriptor();
    co_return udp_socket(std::move(descriptor));
}
```

## Implementation Details

The WebCraft framework implements platform-specific optimizations through:

- `webcraft::async::io::fs::detail::make_file_descriptor()` - Creates platform-optimized file descriptors
- `webcraft::async::io::socket::detail::make_tcp_socket_descriptor()` - Creates platform-optimized TCP socket descriptors  
- `webcraft::async::io::socket::detail::make_tcp_listener_descriptor()` - Creates platform-optimized TCP listener descriptors

All functions return `std::shared_ptr` to the appropriate descriptor types, providing automatic resource management and platform abstraction.