#pragma once

// #include "core.hpp"

// namespace webcraft::async::io::socket
// {
//     struct connection_info
//     {
//         std::string host;
//         uint16_t port;
//     };

//     enum class socket_stream_mode
//     {
//         READ,
//         WRITE
//     };

//     namespace detail
//     {

//         class tcp_descriptor_base
//         {
//         public:
//             tcp_descriptor_base() = default;
//             virtual ~tcp_descriptor_base() = default;

//             virtual task<void> close() = 0; // Close the socket
//         };

//         class tcp_socket_descriptor : public tcp_descriptor_base
//         {
//         public:
//             tcp_socket_descriptor() = default;
//             virtual ~tcp_socket_descriptor() = default;

//             virtual task<void> connect(const connection_info &info) = 0;  // Connect to a server
//             virtual task<size_t> read(std::span<char> buffer) = 0;        // Read data from the socket
//             virtual task<size_t> write(std::span<const char> buffer) = 0; // Write data to the socket
//             virtual task<void> shutdown(socket_stream_mode mode) = 0;     // Shutdown the socket

//             virtual std::string get_remote_host() = 0;
//             virtual uint16_t get_remote_port() = 0;
//         };

//         class tcp_listener_descriptor : public tcp_descriptor_base
//         {
//         public:
//             tcp_listener_descriptor() = default;
//             virtual ~tcp_listener_descriptor() = default;

//             virtual task<void> bind(const connection_info &info) = 0;          // Bind the listener to an address
//             virtual task<void> listen(int backlog) = 0;                        // Start listening for incoming connections
//             virtual task<std::unique_ptr<tcp_socket_descriptor>> accept() = 0; // Accept a new connection
//         };

//         task<std::shared_ptr<tcp_socket_descriptor>> make_tcp_socket_descriptor();
//         task<std::shared_ptr<tcp_listener_descriptor>> make_tcp_listener_descriptor();

//     }

//     class tcp_rstream
//     {
//     private:
//         std::shared_ptr<detail::tcp_socket_descriptor> descriptor;

//     public:
//         tcp_rstream(std::shared_ptr<detail::tcp_socket_descriptor> desc) : descriptor(std::move(desc)) {}
//         ~tcp_rstream() = default;

//         task<size_t> recv(std::span<char> buffer)
//         {
//             return descriptor->read(buffer);
//         }

//         task<char> recv()
//         {
//             std::array<char, 1> buf;
//             co_await recv(buf);
//             co_return buf[0];
//         }

//         task<void> close()
//         {
//             co_await descriptor->shutdown(socket_stream_mode::READ);
//         }
//     };

//     static_assert(async_writable_stream<tcp_rstream, char>);
//     static_assert(async_buffered_writable_stream<tcp_rstream, char>);
//     static_assert(async_closeable_stream<tcp_rstream, char>);

//     class tcp_wstream
//     {
//     private:
//         std::shared_ptr<detail::tcp_socket_descriptor> descriptor;

//     public:
//         tcp_wstream(std::shared_ptr<detail::tcp_socket_descriptor> desc) : descriptor(std::move(desc)) {}
//         ~tcp_wstream() = default;

//         task<size_t> send(std::span<const char> buffer)
//         {
//             return descriptor->write(buffer);
//         }

//         task<bool> send(char b)
//         {
//             std::array<char, 1> buf;
//             buf[0] = b;
//             co_await send(buf);
//             co_return true;
//         }

//         task<void> close()
//         {
//             co_await descriptor->shutdown(socket_stream_mode::WRITE);
//         }
//     };

//     static_assert(async_writable_stream<tcp_wstream, char>);
//     static_assert(async_buffered_writable_stream<tcp_wstream, char>);
//     static_assert(async_closeable_stream<tcp_wstream, char>);

//     class tcp_socket
//     {
//     private:
//         std::shared_ptr<detail::tcp_socket_descriptor> descriptor;
//         std::unique_ptr<tcp_rstream> read_stream;
//         std::unique_ptr<tcp_wstream> write_stream;

//     public:
//         tcp_socket(std::shared_ptr<detail::tcp_socket_descriptor> desc) : descriptor(std::move(desc)) {}
//         ~tcp_socket()
//         {
//             sync_wait(close());
//         }

//         task<void> connect(const connection_info &info)
//         {
//             co_await descriptor->connect(info);
//             read_stream = std::make_unique<tcp_rstream>(descriptor);
//             write_stream = std::make_unique<tcp_wstream>(descriptor);
//         }

//         tcp_rstream &get_readable_stream()
//         {
//             if (!read_stream)
//             {
//                 throw std::runtime_error("Read stream is not initialized.");
//             }
//             return *read_stream;
//         }

//         tcp_wstream &get_writable_stream()
//         {
//             if (!write_stream)
//             {
//                 throw std::runtime_error("Write stream is not initialized.");
//             }
//             return *write_stream;
//         }

//         task<void> shutdown_channel(socket_stream_mode mode)
//         {
//             if (mode == socket_stream_mode::READ && read_stream)
//             {
//                 co_await read_stream->close();
//             }
//             else if (mode == socket_stream_mode::WRITE && write_stream)
//             {
//                 co_await write_stream->close();
//             }
//         }

//         task<void> close()
//         {
//             if (read_stream)
//             {
//                 co_await read_stream->close();
//             }
//             if (write_stream)
//             {
//                 co_await write_stream->close();
//             }

//             if (descriptor)
//             {
//                 co_await descriptor->close();
//             }
//         }
//     };

//     class tcp_listener
//     {
//     private:
//         std::shared_ptr<detail::tcp_listener_descriptor> descriptor;

//     public:
//         tcp_listener(std::shared_ptr<detail::tcp_listener_descriptor> desc) : descriptor(std::move(desc)) {}
//         ~tcp_listener()
//         {
//             if (descriptor)
//             {
//                 sync_wait(descriptor->close());
//             }
//         }

//         task<void> bind(const connection_info &info)
//         {
//             return descriptor->bind(info);
//         }

//         task<void> listen(int backlog)
//         {
//             return descriptor->listen(backlog);
//         }

//         task<std::unique_ptr<tcp_socket>> accept()
//         {
//             auto client_desc = co_await descriptor->accept();
//             co_return std::make_unique<tcp_socket>(std::move(client_desc));
//         }
//     };

//     task<tcp_socket> make_tcp_socket()
//     {
//         auto descriptor = co_await detail::make_tcp_socket_descriptor();
//         co_return tcp_socket(std::move(descriptor));
//     }

//     task<tcp_listener> make_tcp_listener()
//     {
//         auto descriptor = co_await detail::make_tcp_listener_descriptor();
//         co_return tcp_listener(std::move(descriptor));
//     }
// }