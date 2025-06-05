#pragma once
#include <memory>

// for now for PoC we'll use native libraries
// in the future for a more robust solution, we can use either libuv or libevent
// libevent: https://libevent.org/libevent-book/
// libuv: https://docs.libuv.org/en/v1.x/guide/basics.html

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <windows.h>

#elif defined(__linux__)

#ifndef IO_URING_QUEUE_SIZE
// @brief The size of the io_uring queue. This can be adjusted based on the application's needs.
#define IO_URING_QUEUE_SIZE 256
#endif

#include <unistd.h>
#include <liburing.h>

#elif defined(__APPLE__)

#include <unistd.h>
#include <sys/event.h>
#else
// TODO: figure out what other OS's would be using
#endif

namespace webcraft::async
{
    namespace unsafe
    {
#ifdef _WIN32
#include <windows.h>

        using native_runtime_handle = HANDLE;

#elif defined(__linux__)

        using native_runtime_handle = io_uring;
#elif defined(__APPLE__)

        using native_runtime_handle = int; // kqueue file descriptor
#else
// TODO: figure out what other OS's would be using
#endif

        /// @brief Performs the initialization of the runtime handle based on the platform.
        /// This function is unsafe and should be used with caution. It is intended for internal use only.
        /// @param handle the runtime handle to initialize
        void initialize_runtime_handle(native_runtime_handle &handle);
        /// @brief Performs the destruction of the runtime handle based on the platform.
        /// This function is unsafe and should be used with caution. It is intended for internal use only.
        /// @param handle the runtime handle to destroy
        void destroy_runtime_handle(native_runtime_handle &handle);
    }

    /// @brief A class that represents a runtime handle for the async runtime.
    class runtime_handle
    {
    private:
        unsafe::native_runtime_handle handle;

    public:
        /// @brief Constructs a runtime handle and initializes it based on the platform.
        runtime_handle()
        {
            // Create a new runtime handle based on the platform
            unsafe::initialize_runtime_handle(handle);
        }

        /// @brief Destroys the runtime handle and cleans up resources.
        ~runtime_handle()
        {
            // Destroy the runtime handle based on the platform
            unsafe::destroy_runtime_handle(handle);
        }

        /// @brief Gets the native runtime handle.
        /// @return the native runtime handle
        const unsafe::native_runtime_handle &get() const
        {
            return handle;
        }

        /// @brief Gets a pointer to the native runtime handle.
        /// @return the pointer to the native runtime handle
        unsafe::native_runtime_handle *get_ptr()
        {
            return &handle;
        }

        runtime_handle(const runtime_handle &) = delete;
        runtime_handle(runtime_handle &&other) = delete;
        runtime_handle &operator=(const runtime_handle &) = delete;
        runtime_handle &operator=(runtime_handle &&other) = delete;
    };

    /// @brief A class that represents a runtime event that can be used to resume a coroutine.
    class runtime_event
    {
    private:
#ifdef _WIN32
        OVERLAPPED overlapped = {};
#endif
    protected:
        std::coroutine_handle<> handle;
        int result = -1;

    public:
        /// @brief Initializes a runtime event with a coroutine handle and submits it to the kernel
        /// @param h the coroutine handle to be resumed after the event is signaled
        explicit runtime_event(std::coroutine_handle<> h) : handle(h)
        {
#ifdef _WIN32
            ZeroMemory(&overlapped, sizeof(OVERLAPPED));
#endif
        }

#ifdef _WIN32

        OVERLAPPED *get_overlapped() noexcept
        {
            return &overlapped;
        }
#endif

        virtual ~runtime_event() = default;

        /// @brief Resumes the coroutine associated with this event.
        /// @param result the result of the operation
        void resume(int result) noexcept
        {
            if (handle)
            {
                handle.resume();
            }

            this->result = result;
        }

        /// @brief Gets the result of the operation that was signaled by this event.
        /// @return the result of the operation
        constexpr int get_result() const noexcept
        {
            return result;
        }
    };
}