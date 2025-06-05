#include <webcraft/async/config.hpp>
#include <webcraft/async/runtime.hpp>
#include <webcraft/async/executors.hpp>
#include <async/event_signal.h>
#include <iostream>

static webcraft::async::async_runtime_config config = {
    .max_worker_threads = 2 * std::thread::hardware_concurrency(),
    .min_worker_threads = std::thread::hardware_concurrency(),
    .idle_timeout = 30s,
    .worker_strategy = webcraft::async::worker_strategy_type::PRIORITY};

/// Macro to generate helper setter methods for the cofig
#define set_async_runtime_config_field(type, field)           \
    void webcraft::async::runtime_config::set_##field(type t) \
    {                                                         \
        config.field = t;                                     \
    }

set_async_runtime_config_field(size_t, max_worker_threads);
set_async_runtime_config_field(size_t, min_worker_threads);
set_async_runtime_config_field(std::chrono::milliseconds, idle_timeout);
set_async_runtime_config_field(webcraft::async::worker_strategy_type, worker_strategy);

webcraft::async::async_runtime &webcraft::async::async_runtime::get_instance()
{
    // lazily initialize the instance (allow for config setup before you get the first instance)
    static async_runtime runtime(config);
    return runtime;
}

webcraft::async::async_runtime::async_runtime(async_runtime_config &config)
{
    webcraft::async::executor_service_params params = {
        .minWorkers = config.min_worker_threads,
        .maxWorkers = config.max_worker_threads,
        .idleTimeout = config.idle_timeout,
        .strategy = config.worker_strategy};

    this->executor_svc.reset(new webcraft::async::executor_service(*this, params));
    this->timer_svc.reset(new webcraft::async::timer_service(*this));
    this->io_svc.reset(new webcraft::async::io::io_service(*this));
}

webcraft::async::io::io_service &webcraft::async::async_runtime::get_io_service()
{
    return *(this->io_svc);
}

webcraft::async::executor_service &webcraft::async::async_runtime::get_executor_service()
{
    return *(this->executor_svc);
}

webcraft::async::timer_service &webcraft::async::async_runtime::get_timer_service()
{
    return *(this->timer_svc);
}

webcraft::async::async_runtime::~async_runtime()
{
}

void webcraft::async::async_runtime::queue_task_resumption(std::coroutine_handle<> h)
{
    struct yield_event : public runtime_event
    {
        unsigned long ptr;
        webcraft::async::runtime_handle &handle;

        yield_event(webcraft::async::runtime_handle &handle, std::coroutine_handle<> h) : runtime_event(h), handle(handle)
        {
#ifdef _WIN32
            if (!PostQueuedCompletionStatus(
                    handle.get(),
                    0,                                 // bytes transferred
                    reinterpret_cast<ULONG_PTR>(this), // completion key
                    this->get_overlapped()))           // overlapped structure
            {
                throw std::runtime_error("Failed to queue task resumption: " + std::to_string(GetLastError()));
            }

#elif defined(__linux__)

            auto *sqe = io_uring_get_sqe(handle.get_ptr());
            if (sqe == nullptr)
            {
                throw std::runtime_error("Failed to get SQE from io_uring");
            }

            io_uring_sqe_set_data(sqe, this);
            io_uring_prep_nop(sqe); // Prepare a NOP operation to signal the completion of the task

#elif defined(__APPLE__)

            ptr = (uintptr_t)h.address();
            {
                struct kevent kev;
                EV_SET(&kev, ptr, EVFILT_USER, EV_ADD | EV_CLEAR, 0, 0, this);
                if (kevent(handle.get(), &kev, 1, nullptr, 0, nullptr) < 0)
                {
                    throw std::runtime_error("Failed to register task resumption: " + std::to_string(errno));
                }
            }
            {
                struct kevent kev;
                EV_SET(&kev, ptr, EVFILT_USER, 0, NOTE_TRIGGER, 0, this);
                if (kevent(handle.get(), &kev, 1, nullptr, 0, nullptr) < 0)
                {
                    throw std::runtime_error("Failed to queue task resumption: " + std::to_string(errno));
                }
            }
#else
#endif
        }

        ~yield_event()
        {
#ifdef __APPLE__

            struct kevent kev;
            EV_SET(&kev, ptr, EVFILT_USER, EV_DELETE, 0, 0, this);
            if (kevent(handle.get(), &kev, 1, nullptr, 0, nullptr) < 0)
            {
                std::cerr << "Failed to unregister task resumption: " << std::to_string(errno) << std::endl;
                std::exit(1);
            }
#endif
        }
    };

    new yield_event(this->handle, h);
}

void webcraft::async::async_runtime::run(webcraft::async::task<void> &&t)
{
    struct final_awaiter
    {

        struct promise_type
        {
            auto get_return_object() { return final_awaiter{}; }
            auto initial_suspend() { return std::suspend_never(); }
            auto final_suspend() noexcept { return std::suspend_never(); }
            void unhandled_exception() {}
            void return_void() {}
        };
    };

    auto fn = [this](webcraft::async::task<void> &&t, ::async::event_signal &ev) mutable -> final_awaiter
    {
        // Wait for the task to finish
        co_await t;

        // Set the event to signal that the task is done
        shutdown();
    };

    auto _ = fn(std::move(t), ev);

    while (!ev.is_set())
    {
        runtime_event *event = nullptr;
#ifdef _WIN32

        // Windows IOCP
        DWORD bytesTransferred;
        ULONG_PTR completionKey;
        LPOVERLAPPED overlapped;

        if (GetQueuedCompletionStatus(this->handle.get(), &bytesTransferred, &completionKey, &overlapped, INFINITE))
        {
            event = reinterpret_cast<runtime_event *>(completionKey);
            if (event)
            {
                event->resume(bytesTransferred); // Resume the event
                delete event;
                event = nullptr; // Clear the event pointer
            }
        }

#elif defined(__linux__)
        // io_uring_submit_sqe
        io_uring_submit_and_wait(this->handle.get_ptr(), 1); // TODO: look into splitting this into io_uring_submit & io_uring_wait_cqe
        io_uring_cqe *cqe;
        int head;
        int processed = 0;

        io_uring_for_each_cqe(this->handle.get_ptr(), head, cqe)
        {
            event = static_cast<runtime_event *>(io_uring_cqe_get_data(cqe));
            if (event)
            {
                event->resume(cqe->res); // Resume the event
                processed++;
                delete event;
                event = nullptr; // Clear the event pointer
            }
        }

        io_uring_cq_advance(this->handle.get_ptr(), processed); // Advance the completion queue

#elif defined(__APPLE__)

        // kqueue
        struct kevent kev;
        struct timespec timeout = {0, 0}; // No timeout, wait indefinitely

        int nev = kevent(this->handle.get(), nullptr, 0, &kev, 1, &timeout);
        if (nev < 0)
        {
            throw std::runtime_error("Failed to wait for kqueue event: " + std::to_string(errno));
        }

        if (nev > 0)
        {
            event = reinterpret_cast<runtime_event *>(kev.udata);
            if (event)
            {
                event->resume(kev.data); // Resume the event
                delete event;
                event = nullptr; // Clear the event pointer
            }
        }
#else

#endif
    }
}

void webcraft::async::unsafe::initialize_runtime_handle(webcraft::async::unsafe::native_runtime_handle &handle)
{
#ifdef _WIN32
    // iocp https://stackoverflow.com/questions/53651391/which-handle-to-pass-to-createiocompletionport-if-using-iocp-for-basic-signalin
    WSAData wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) // Initialize Winsock
    {
        throw std::runtime_error("Failed to initialize Winsock: " + std::to_string(WSAGetLastError()));
    }

    handle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0); // Create an IOCP handle
    if (handle == nullptr)
    {
        throw std::runtime_error("Failed to create IOCP handle: " + std::to_string(GetLastError()));
    }
#elif defined(__linux__)
    // io_uring https://pabloariasal.github.io/2022/11/12/couring-1/
    if (io_uring_queue_init(IO_URING_QUEUE_SIZE, &handle, 0) < 0) // Initialize the io_uring queue with a size of 256
    {
        throw std::runtime_error("Failed to initialize io_uring queue");
    }
#elif defined(__APPLE__)
    // kqueue https://repo.or.cz/eleutheria.git/blob/master:/kqueue/kqclient.c
    handle = kqueue(); // Create a kqueue handle
    if (handle == -1)
    {
        throw std::runtime_error("Failed to create kqueue handle: " + std::to_string(errno));
    }
#else
#endif
}

void webcraft::async::unsafe::destroy_runtime_handle(webcraft::async::unsafe::native_runtime_handle &handle)
{
#ifdef _WIN32
    CloseHandle(handle); // Close the IOCP handle
    WSACleanup();        // Cleanup Winsock
#elif defined(__linux__)
    io_uring_queue_exit(&handle); // exit the io_uring queue
#elif defined(__APPLE__)
    close(handle); // Close the kqueue handle
#else
#endif
}
