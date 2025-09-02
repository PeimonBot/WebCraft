# WebCraft

NOTE: This codebase is recommended to run on C++ 23 since most of the features are geared to it. If its possible try to get your compiler to compile with experimental libraries enabled (which will enable all the features the compiler may not have implemented for C++ 23).

The following compilers (and anything newer) are supported: GCC 13, MSVC 2022, Clang 17

## Features / Modules

1. Asynchronous Runtime, Supporting Yielding and working with timeouts that are not thread based
2. Basic networking API (Sockets, Server Sockets, async read & write) via I/O streams
3. Web API and WebApplication wrapper (everything before was mostly console or cmdline based)
4. Raw request/response handling with streams vs composable stream adaptor based handling (both are supported by nature)
5. TLS support for server and client side

The features 1 & 2 are supported in v0.5. The features 3 & 4 will be available in v1. TLS may arrive in v2.
