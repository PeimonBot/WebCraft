# Async Runtime Package: webcraft::async

## Awaitable Concepts

The webcraft::async namespace provides comprehensive concepts for working with awaitables:

```cpp
/// \brief Concept that checks if a type is a valid suspend type for await_suspend
template <typename T>
concept is_awaitable_suspend_type = std::same_as<T, void> || 
                                   std::convertible_to<T, bool> || 
                                   std::convertible_to<T, std::coroutine_handle<>>;

/// \brief Concept that checks if a type has member operator co_await
template <typename T>
concept has_member_operator_co_await_v = requires(T &&t) {
    { t.operator co_await() };
};

/// \brief Concept that checks if a type has non-member operator co_await
template <typename T>
concept has_non_member_operator_co_await_v = requires(T &&t) {
    { operator co_await(t) };
};

/// \brief Concept that checks if a type has awaitable elements
template <typename T>
concept has_awaitable_elements = requires(T &&a, std::coroutine_handle<> h) {
    { a.await_ready() } -> std::convertible_to<bool>;
    { a.await_suspend(h) } -> is_awaitable_suspend_type;
    { a.await_resume() };
};

/// \brief Main awaitable concept
template <typename T>
concept awaitable_t = has_member_operator_co_await_v<T> || 
                     has_awaitable_elements<T> || 
                     has_non_member_operator_co_await_v<T>;

/// \brief Type alias to get the resume type of an awaitable
template <awaitable_t T>
using awaitable_resume_t = decltype(detail::get_awaiter(std::declval<T>()).await_resume());


```

## Task<T>

The fundamental coroutine type in `webcraft::async`. A `task<T>` represents an asynchronous operation that produces a result of type `T`. Implementation inspired by [microsoft/cpp-async](https://github.com/microsoft/cpp-async) and [lewissbaker/cppcoro](https://github.com/lewissbaker/cppcoro/). The task type implementation is an eager task (it starts as soon as its constructed, similar to JS Promises):

```cpp
template <typename T = void>
class task
{
public:
    using promise_type = task_promise<T>;
    using handle_type = std::coroutine_handle<promise_type>;

    // Move-only type
    task(task &&other) noexcept;
    task &operator=(task &&other) noexcept;
    ~task();

    task(const task &) = delete;
    task &operator=(const task &) = delete;

    // Awaitable interface
    bool await_ready() const noexcept;
    void await_suspend(std::coroutine_handle<> h) noexcept;
    T await_resume(); // Returns void for task<void>
};
```

### Promise Type Specification

The `task_promise<T>` type defines the coroutine promise interface that controls the behavior of task coroutines:

```cpp
template <typename T>
class task_promise
{
public:
    std::optional<T> value;
    std::exception_ptr exception;
    std::coroutine_handle<> continuation;

    // Required promise interface
    task<T> get_return_object();
    
    // Eager execution: returns suspend_never to start immediately
    std::suspend_never initial_suspend() noexcept;
    
    // Custom final awaiter for continuation chaining
    struct final_awaiter
    {
        bool await_ready() noexcept;
        void await_suspend(std::coroutine_handle<task_promise> h) noexcept;
        void await_resume() noexcept;
    };
    final_awaiter final_suspend() noexcept;
    
    // Store the result value
    void return_value(T v) noexcept;
    
    // Handle exceptions
    void unhandled_exception() noexcept;
};

// Specialization for void return type
template <>
class task_promise<void>
{
public:
    std::exception_ptr exception;
    std::coroutine_handle<> continuation;

    task<void> get_return_object();
    std::suspend_never initial_suspend() noexcept;
    final_awaiter final_suspend() noexcept;
    
    void return_void() noexcept;  // void tasks use return_void()
    void unhandled_exception() noexcept;
};
```

## Synchronization Primitives

### event_signal

A blocking synchronization primitive that can be set and waited upon. You can have multiple waiters waiting on this event to resume execution:

```cpp
class event_signal
{
public:
    event_signal();
    
    void set() noexcept;
    void reset() noexcept;
    bool is_set() const noexcept;
    
    bool wait() const;
    bool wait_for(std::chrono::milliseconds timeout) const;
    
    bool operator()() const;
    explicit operator bool() const;
};
```

### async_event

An asynchronous event that can be awaited in coroutines. You can have multiple awaiters waiting on this event to resume execution asynchronously:

```cpp
struct async_event
{
    bool await_ready();
    void await_suspend(std::coroutine_handle<> h);
    void await_resume();
    
    void set();
    bool is_set() const;
};
```

## Task Completion and Control

### task_completion_source<T>

Allows manual completion of a task from external code (e.g thread pool work, callback, etc). Implementation inspired by [microsoft/cpp-async](https://github.com/microsoft/cpp-async):

```cpp
template <typename T>
class task_completion_source
{
public:
    using value_type = T;
    
    task_completion_source() noexcept;
    
    void set_value(T value);  // void set_value() for T=void
    void set_exception(std::exception_ptr exception);
    
    webcraft::async::task<T> task();
};
```

### fire_and_forget_task

A task type that runs without needing to be awaited. It will destroy itself once the coroutine has finished (reached final suspend or coroutine frame is destroyed via `h.destroy()`):

```cpp
class fire_and_forget_task
{
public:
    class promise_type
    {
    public:
        fire_and_forget_task get_return_object();
        std::suspend_never initial_suspend() noexcept;
        std::suspend_never final_suspend() noexcept;
        void return_void() noexcept;
        void unhandled_exception() noexcept;
    };
};

fire_and_forget_task fire_and_forget(task<void> t);
```

## Combinators and Utilities

### when_all

Execute multiple tasks concurrently and wait for all to complete asynchronously:

```cpp
// For tasks that return values
template <std::ranges::input_range Range>
    requires awaitable_t<std::ranges::range_value_t<Range>>
task<std::vector<Result>> when_all(Range &&tasks);

// For void-returning tasks
template <std::ranges::input_range Range>
    requires awaitable_t<std::ranges::range_value_t<Range>> && std::is_void_v<Result>
task<void> when_all(Range &&tasks);

// For tuples of different task types
template <typename... Tasks>
    requires(awaitable_t<Tasks> && ...)
task<std::tuple<normalized_result_t<Tasks>...>> when_all(Tasks &&...tasks);
```

### when_any

Execute multiple tasks concurrently and return the first one to complete asynchronously:

```cpp
// Type alias for normalized results (void becomes std::monostate)
template <typename T>
using normalized_result_t = std::conditional_t<
    std::is_void_v<awaitable_resume_t<T>>,
    std::monostate,
    awaitable_resume_t<T>>;

// For range of tasks
template <std::ranges::input_range Range>
    requires awaitable_t<std::ranges::range_value_t<Range>>
task<Result> when_any(Range &&tasks);

// For tuple of different task types
template <awaitable_t... Tasks>
task<std::variant<normalized_result_t<Tasks>...>> when_any(Tasks &&...tasks);
```

### sync_wait

Block the current thread and wait for an awaitable to complete. Implementation inspired by C++ 26 `sync_wait` feature:

```cpp
template <awaitable_t T>
awaitable_resume_t<T> sync_wait(T &&awaitable);
```

## Generators

### generator<T>

A synchronous generator for producing sequences of values. Implementation borrowed from [lewissbaker/cppcoro](https://github.com/lewissbaker/cppcoro/):

```cpp
template <typename T>
class generator
{
public:
    class iterator;
    
    iterator begin();
    iterator end();
    
    // Range interface
};
```

### async_generator<T>

An asynchronous generator for producing sequences of values with async operations. Implementation borrowed from [lewissbaker/cppcoro](https://github.com/lewissbaker/cppcoro/):

```cpp
template <typename T>
class async_generator
{
public:
    class iterator;
    
    task<iterator> begin();
    iterator end();
    
    // Async range interface
};
```

## Thread Pool

A cached thread pool implementation for CPU-bound tasks:

```cpp
class thread_pool
{
public:
    thread_pool(size_t min_threads = 0, 
                size_t max_threads = std::thread::hardware_concurrency(), 
                std::chrono::milliseconds idle_timeout = 10'000ms);
    
    ~thread_pool();
    
    // Submit a callable to be executed on the thread pool
    template <typename F, typename... Args>
    auto submit(F &&f, Args &&...args) -> std::future<std::invoke_result_t<F, Args...>>;
    
    // Getters
    size_t get_min_threads() const;
    size_t get_max_threads() const;
    std::chrono::milliseconds get_idle_timeout() const;
    size_t get_workers_size() const;
    size_t get_available_workers() const;
};
```

## Macros and Type Aliases

The namespace also provides convenient macros and type aliases:

```cpp
// Macro for creating async lambdas
#define co_async [&]() -> ::webcraft::async::task<void>

// Type alias for tasks
#define async_t(T) ::webcraft::async::task<T>
