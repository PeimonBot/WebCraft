# WebCraft

NOTE: This codebase is recommended to run on C++ 23 since most of the features are geared to it. If its possible try to get your compiler to compile with experimental libraries enabled (which will enable all the features the compiler may not have implemented for C++ 23).

The following compilers (and anything newer) are supported: GCC 13, MSVC 2022, Clang 17

## Features / Modules

- Asynchronous Runtime and Executors (IO bound - not public.. mostly internal and CPU bound executors - public) - works if enabled
- Basic networking API (Sockets, Server Sockets, async read & write)
- Web API and WebApplication wrapper (everything before was mostly console or cmdline based)
- Context & Dependency Injection and Application Builder framework
- Raw request/response handling with streams
- DataTransferObject API and serialization
- potentially support of creating adapters for other servers
- eventually have TLS support, browser rendering, etc

## Async Runtime

This codebase uses C++ 20 coroutines to support asynchronous programming. Most examples can be found in [/tests/src/test_task.cpp](/tests/src/test_task.cpp) and [/tests/src/test_runtime.cpp](/tests/src/test_runtime.cpp)

The codebase supports being able to yield control back to the runtime, being able to spawn asynchronous tasks like in languages like JavaScript & TypeScript, C#, Python, and Rust, being able to sleep asynchronously without the usage of threads, being able to chain tasks together by piping `then()`, and being able to have multiple tasks complete asynchronously using `when_all` and `when_any`.

### Async IO

The way I treat AsyncIO in my framework is with 2 concepts: Pub/Sub & Streams. Pub Sub is the concept where:
 - you have subscribers subscribed to publishers and receiving messages from them
 - messages can be either values, errors, or completion status (`onNext`, `onError`, `onCompletion`, `onSubscribe`)
 - subscribers can cancel their subscription

Streams are the concept where you have 2 types: InputStreams and OutputStreams:
 - You can receive items from InputStreams (if they still are there otherwise it'll give you `std::nullopt`)
 - You can send items to OutputStreams
 - Streams can be closed and asynchronous streams will resume once the task is complete

The idea is that Pub Sub is very closely related to I/O Streams in the sense that:
 - OutputStreams are the frontend publishers whose subscribers are the areas which are being written to (which is only 1 since its a stream)
 - InputStreams are the subscribers to published events whose publishers read from
So in a nutshell: OutputStreams = Publisher (to 1 subscriber) and InputStreams are a subscriber

