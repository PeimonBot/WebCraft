# Asynchronous I/O Implemented in WebCraft

This readme will go over how WebCraft handles **Asynchronous I/O** powered with the latest C++ coroutine features.

Table of Contents:

1. [Async Streams](#async-streams)
2. [Async Readable Stream Adaptors](#async-readable-stream-adaptors)
3. [Async File I/O](#async-file-io)
4. [Async Socket I/O](#async-socket-io)

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

### Channels

Channels are a mechanism to transfer data from a publisher to a subscriber. The model that we have implemented our channels is through MPSC (multiple publishers to a single consumer - since it only makes sense to deal with one event at a time).
You can create an MPSC channel as shown below (NOTE: you have to specify data type of channel otherwise what data will you be sending over in the first place):
```cpp
auto [rstream, wstream] = make_mpsc_channel<int>();
```
The type of `rstream` satisfies `async_readable_stream` and the type of `wstream` satisfies `async_writable_stream`. This effectively is an asynchronous pipe. Concurrency here is not required to be a concern since whenever the "send()" on the writeable stream occurs, we resume the existing read.

**NOTE: DO NOT TRY AND PIPE `rstream` into `wstream` as it will cause an infinite loop (more so a stackoverflow exception) since all values received from read will be sent into write which will be sent into read and you get the rest.**

Working with this becomes really useful as you can build highly scalable Publisher Subscriber Applications based off of channels as your data sending medium. Most microservices use this message queues which internally uses channels since it makes working with event streams a lot easier.
I myself am planning on using channels for managing async socket I/O and async file I/O.

## Async Readable Stream Adaptors

Stream's aren't really useful by themselves. Most of the time, we want to turn our raw data into something useful to then deal with it. This is the idea of a **stream adaptor**. We take a readable stream of one data type, then we apply some kind of operation on it (mapping, filtering, transforming), then we get a stream of another data type, something more useful to deal with.

Here is an example:

Suppose, we want to group the students into a map where we assign a lesson grade (A for 80-100, B for 70-80, C for 60-70, and D for 50-60) as showing and get rid of any students which are failing and have the students sorted in each grouping in order:
```cpp
struct student {
    std::string name;
    std::vector<double> marks;
    std::string grade;
};
```

The non-adaptor based solution would look something like this:
```cpp
task<std::unordered_map<std::string, std::vector<student>>> get_student_grade_groupings(async_readable_stream<student> students) {
    std::unordered_map<std::string, student> map;
    map["A"] = {};
    map["B"] = {};
    map["C"] = {};
    map["D"] = {};
    while (auto st = co_await students)
    {
        std::vector<double> marks = st.marks;
        double sum_of_marks = std::accumulate(marks.begin(), marks.end(), 0.0));
        size_t num_of_marks = marks.size();
        double average = sum_of_marks / num_of_marks;

        if (average >= 80.0) {
            map["A"].push_back(*st);
        } else if (average >= 70.0) {
            map["B"].push_back(*st);
        } else if (average >= 60.0) {
            map["C"].push_back(*st);
        } else if (average >= 50.0) {
            map["D"].push_back(*st);
        }
    }

    std::sort(map["A"]);
    std::sort(map["B"]);
    std::sort(map["C"]);
    std::sort(map["D"]);

    return map;
}
```

The adaptor based solution would be as follows:
```cpp
std::string average_to_grade(double average) {
    if (average >= 80.0) {
        return "A";
    } else if (average >= 70.0) {
        return "B";
    } else if (average >= 60.0) {
        return "C";
    } else if (average >= 50) {
        return "D";
    } else {
        return "F";
    }
}

double get_average(student st) {
    std::vector<double> marks = st.marks;
    double sum_of_marks = std::accumulate(marks.begin(), marks.end(), 0.0));
    size_t num_of_marks = marks.size();
    double average = sum_of_marks / num_of_marks;
    return average;
}


task<std::unordered_map<std::string, std::vector<student>>> get_student_grade_groupings(async_readable_stream<student> students) {
    return students | filter([](auto st) {
        return get_average(st) >= 50;
    }) | sorted([](auto pair) {
        return pair.key; 
    }) | group_by([](auto st) {
        return average_to_grade(get_average(st));
    });
}
```

This is just one example which would greatly reduce the amount of code and logic involved to write a program. There are many other uses for having async streams including when dealing with pub/sub streams.

### Some of the adaptors have already been implemented in this framework:

All stream adaptors have to inherit the `async_readable_stream_adaptor`. The definition of it is shown below:

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

Definition is shown below:
```cpp
template <typename InType, typename Func>
auto transform(Func &&fn) -> std::is_derived_from<async_readable_stream_adaptor>;
```

Using this you'll be able transform the existing async_readable_stream to another async_readable_stream. The function that is passed has to be of signature `async_generator<OutType>(async_generator<InType>)` where `OutType` is the transformed readable stream passed from the `transform` function. Some examples of this are shown below:

```cpp
mock_readable_stream stream({1,2,3,4,5});

async_readable_stream<std::string> auto new_stream = stream | transform([](async_generator<int> gen) {
    for_each_async(value, gen, {
        co_yield std::to_string(value);
        co_yield std::to_string(value * 2);
    });
});
// the value of this is ["1", "2", "2", "4", "3", "6", "4", "8", "5", "10"]
```

#### Map adaptor

Definition is shown below:
```cpp
template <typename InType, typename Func, typename OutType = std::invoke_result_t<Func, InType>>
auto map(Func &&fn) -> std::is_derived_from<async_readable_stream_adaptor>;
```
Using this adaptor, you create a new readable stream which has the values from the old stream mapped using the function passed. An example of this is shown below:

```cpp
mock_readable_stream stream({1,2,3,4,5});

async_readable_stream<std::string> auto new_stream = stream | map([](int value) {
    return std::to_string(value);
});
// the value of this is ["1", "2", "3", "4", "5"]
```

#### Pipe adaptor

Definition is shown below:
```cpp
template <typename T>
    requires std::is_copy_assignable_v<T>
auto pipe(async_writable_stream<T> auto &str) -> std::is_derived_from<async_readable_stream_adaptor>;
```
Using this adaptor, you create a new readable stream on which when read, also forwards the read value into the writable stream provided. An example is shown below:

```cpp
mock_readable_stream<int> rstream({1,2,3,4,5});
mock_writable_stream<int> wstream;

async_readable_stream<int> auto new_stream = rstream | pipe(wstream);

while (auto opt = co_await new_stream.recv()) {
    assert(wstream.received_value(*opt));
}
```

#### Filter adaptor

Definition is shown below:
```cpp
template <typename T, typename Func>
auto filter(Func &&predicate) -> std::is_derived_from<async_readable_stream_adaptor>;
```

Using this adaptor, you'd be able to filter out values in the streams which you do want to retain. An example is shown below:
```cpp
mock_readable_stream<int> stream({1,2,3,4,5});
async_readable_stream<int> auto new_stream = stream | filter([](int value) { return value % 2 == 0; });
// streams returned is [2,4]
```

#### Limit adaptor

Definition is shown below:
```cpp
template <typename T>
auto limit(size_t size) -> std::is_derived_from<async_readable_stream_adaptor>;
```

Using this adaptor, you'd be able to limit the amount of values sent through the stream. An example is shown below:
```cpp
mock_readable_stream<int> stream({1,2,3,4,5});
async_readable_stream<int> auto new_stream = stream | limit(3);
// streams returned is [1,2,3]
```

#### Skip adaptor

Definition is shown below:
```cpp
template <typename T>
auto skip(size_t size) -> std::is_derived_from<async_readable_stream_adaptor>;
```

Using this adaptor, you'd be able to skip the amount of values sent through the stream. An example is shown below:
```cpp
mock_readable_stream<int> stream({1,2,3,4,5});
async_readable_stream<int> auto new_stream = stream | skip(2);
// streams returned is [3,4,5]
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

## Async Socket I/O