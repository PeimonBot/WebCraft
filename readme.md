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

The way I am treating AsyncIO is inspired by Java Streams and a couple other functional programming frameworks. I really liked Java Streams (it made everything much simpler but I'm only mentioning this since I came from a Java world). I also really like the Ranges API of C++, LINQ of C#, and I couple others and I believe that both the I/O streams and functional programming should inherintly be together.
So I have modeled the framework based off of input and output streams which can be adapted to use common functional programming concepts and be transformed and translated into streams of other types using `async_generator` and other powerful types derived from C++ 20. Right now it is a work in progress but I will update the feature list once its done.

When in doubt, the test cases are the documentation!