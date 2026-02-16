#pragma once

///////////////////////////////////////////////////////////////////////////////
// Copyright (c) Aditya Rao
// Licenced under MIT license. See LICENSE.txt for details.
///////////////////////////////////////////////////////////////////////////////

#include "core.hpp"
#include <webcraft/async/fire_and_forget_task.hpp>
#include <stdexcept>

namespace webcraft::async::io::socket
{
    struct connection_info
    {
        std::string host;
        uint16_t port;
    };

    /// Placeholder for options when joining a multicast group. Currently empty: an instance
    /// of this type indicates default multicast join behavior. Fields may be added in the future.
    struct multicast_join_options
    {
    };

    namespace detail
    {
        /// Returns true if the given address string is a valid IPv4 or IPv6 multicast address.
        inline bool is_multicast_address(const std::string &addr)
        {
            if (addr.empty()) return false;
            if (addr.find('.') != std::string::npos)
            {
                // IPv4: 224.0.0.0 - 239.255.255.255
                unsigned a = 0, b = 0, c = 0, d = 0;
                int n = 0;
                if (std::sscanf(addr.c_str(), "%u.%u.%u.%u%n", &a, &b, &c, &d, &n) != 4) return false;
                if (addr.size() != static_cast<std::size_t>(n)) return false;
                if (a > 255u || b > 255u || c > 255u || d > 255u) return false;
                if (a < 224u || a > 239u) return false;
                return true;
            }
            if (addr.find(':') != std::string::npos)
            {
                // IPv6: ff00::/8 — first 16-bit segment must be 0xff00–0xffff
                std::size_t i = 0;
                while (i < addr.size() && addr[i] == ':') ++i;
                while (i < addr.size())
                {
                    std::size_t start = i;
                    while (i < addr.size() && addr[i] != ':')
                    {
                        char ch = addr[i];
                        if ((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F'))
                            ++i;
                        else
                            return false;
                    }
                    if (i > start)
                    {
                        if (i - start >= 2u && i - start <= 4u)
                        {
                            if ((addr[start] == 'f' || addr[start] == 'F') && (addr[start + 1] == 'f' || addr[start + 1] == 'F'))
                                return true;
                        }
                        return false;
                    }
                    if (i < addr.size()) ++i;
                }
                return false;
            }
            return false;
        }
    }

    /// Represents a multicast group address. Use resolve() to create from a string (e.g. "239.255.0.1").
    struct multicast_group
    {
        std::string host;  ///< Multicast group address (e.g. "239.255.0.1")
        uint16_t port{0};  ///< Port used when sending to the group; must be set to the desired (non-zero) UDP port before calling send functions.

        /// Resolve a multicast group from an address string (IPv4 or IPv6 multicast address).
        /// \throws std::invalid_argument if addr is not a valid multicast address.
        static multicast_group resolve(std::string_view addr)
        {
            std::string s(addr);
            if (!detail::is_multicast_address(s))
                throw std::invalid_argument("Not a multicast address: " + s);
            multicast_group g;
            g.host = std::move(s);
            return g;
        }
    };

    enum class ip_version
    {
        IPv4,
        IPv6
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
            virtual void shutdown(socket_stream_mode mode) = 0;           // Shutdown the socket

            virtual std::string get_remote_host() = 0;
            virtual uint16_t get_remote_port() = 0;
        };

        class tcp_listener_descriptor : public tcp_descriptor_base
        {
        public:
            tcp_listener_descriptor() = default;
            virtual ~tcp_listener_descriptor() = default;

            virtual void bind(const connection_info &info) = 0;                // Bind the listener to an address
            virtual void listen(int backlog) = 0;                              // Start listening for incoming connections
            virtual task<std::shared_ptr<tcp_socket_descriptor>> accept() = 0; // Accept a new connection
        };

        class udp_socket_descriptor
        {
        public:
            udp_socket_descriptor(std::optional<ip_version> version) {}
            virtual ~udp_socket_descriptor() = default;

            virtual task<void> close() = 0;
            virtual void bind(const connection_info &info) = 0;

            virtual task<size_t> recvfrom(std::span<char> buffer, connection_info &info) = 0;
            virtual task<size_t> sendto(std::span<const char> buffer, const connection_info &info) = 0;

            /// Join a multicast group. Optional; no-op if not supported (e.g. mock).
            virtual void join_group(const multicast_group &group, const multicast_join_options &opts) { (void)group; (void)opts; }
            /// Leave a multicast group.
            virtual void leave_group(const multicast_group &group) { (void)group; }
        };

        std::shared_ptr<tcp_socket_descriptor> make_tcp_socket_descriptor();
        std::shared_ptr<tcp_listener_descriptor> make_tcp_listener_descriptor();
        std::shared_ptr<udp_socket_descriptor> make_udp_socket_descriptor(std::optional<ip_version> version = std::nullopt);

    }

    class tcp_rstream
    {
    private:
        std::shared_ptr<detail::tcp_socket_descriptor> descriptor;

    public:
        explicit tcp_rstream(std::shared_ptr<detail::tcp_socket_descriptor> desc) : descriptor(desc) {}
        ~tcp_rstream() = default;
        tcp_rstream(tcp_rstream &) = delete;
        tcp_rstream &operator=(tcp_rstream &) = delete;
        tcp_rstream(tcp_rstream &&other) : descriptor(std::exchange(other.descriptor, nullptr)) {}
        tcp_rstream &operator=(tcp_rstream &&other)
        {
            if (this != &other)
            {
                descriptor = std::exchange(other.descriptor, nullptr);
            }
            return *this;
        }

        task<size_t> recv(std::span<char> buffer)
        {
            return descriptor->read(buffer);
        }

        task<std::optional<char>> recv()
        {
            std::array<char, 1> buf;
            if (co_await recv(buf))
            {
                co_return buf[0];
            }
            co_return std::nullopt;
        }

        task<void> close()
        {
            descriptor->shutdown(socket_stream_mode::READ);
            co_return;
        }
    };

    static_assert(async_readable_stream<tcp_rstream, char>);
    static_assert(async_buffered_readable_stream<tcp_rstream, char>);
    static_assert(async_closeable_stream<tcp_rstream, char>);

    class tcp_wstream
    {
    private:
        std::shared_ptr<detail::tcp_socket_descriptor> descriptor;

    public:
        explicit tcp_wstream(std::shared_ptr<detail::tcp_socket_descriptor> desc) : descriptor(desc) {}
        ~tcp_wstream() = default;
        tcp_wstream(tcp_wstream &) = delete;
        tcp_wstream &operator=(tcp_wstream &) = delete;
        tcp_wstream(tcp_wstream &&other) : descriptor(std::exchange(other.descriptor, nullptr)) {}
        tcp_wstream &operator=(tcp_wstream &&other)
        {
            if (this != &other)
            {
                descriptor = std::exchange(other.descriptor, nullptr);
            }
            return *this;
        }

        task<size_t> send(std::span<const char> buffer)
        {
            return descriptor->write(buffer);
        }

        task<bool> send(char b)
        {
            std::array<char, 1> buf;
            buf[0] = b;
            if (co_await send(buf))
            {
                co_return true;
            }
            co_return false;
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
                throw std::logic_error("Descriptor is null");

            co_await descriptor->connect(info);
        }

        tcp_rstream &get_readable_stream()
        {
            if (!descriptor)
                throw std::logic_error("Descriptor is null");
            return read_stream;
        }

        tcp_wstream &get_writable_stream()
        {
            if (!descriptor)
                throw std::logic_error("Descriptor is null");
            return write_stream;
        }

        void shutdown_channel(socket_stream_mode mode)
        {
            if (!descriptor)
                throw std::logic_error("Descriptor is null");
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
            co_return tcp_socket(co_await descriptor->accept());
        }

        task<void> close()
        {
            if (descriptor)
            {
                co_await descriptor->close();
                descriptor.reset();
            }
        }
    };

    class udp_socket
    {
    private:
        std::shared_ptr<detail::udp_socket_descriptor> descriptor;

    public:
        udp_socket(std::shared_ptr<detail::udp_socket_descriptor> desc) : descriptor(std::move(desc)) {}
        ~udp_socket()
        {
            fire_and_forget(close());
        }

        task<void> close()
        {
            if (descriptor)
            {
                co_await descriptor->close();
                descriptor.reset();
            }
        }

        void bind(const connection_info &info)
        {
            descriptor->bind(info);
        }

        void join(const multicast_group &group)
        {
            descriptor->join_group(group, multicast_join_options{});
        }

        void leave(const multicast_group &group)
        {
            descriptor->leave_group(group);
        }

        task<size_t> recvfrom(std::span<char> buffer, connection_info &info)
        {
            return descriptor->recvfrom(buffer, info);
        }

        task<size_t> sendto(std::span<const char> buffer, const connection_info &info)
        {
            return descriptor->sendto(buffer, info);
        }

        task<size_t> sendto(std::span<const char> buffer, const multicast_group &group)
        {
            connection_info info{group.host, group.port};
            return descriptor->sendto(buffer, info);
        }
    };

    /// Alias for UDP socket used in multicast contexts (same type; join/leave/sendto(group) available).
    using multicast_socket = udp_socket;

    inline tcp_socket make_tcp_socket()
    {
        return tcp_socket(detail::make_tcp_socket_descriptor());
    }

    inline tcp_listener make_tcp_listener()
    {
        return tcp_listener(detail::make_tcp_listener_descriptor());
    }

    inline udp_socket make_udp_socket(std::optional<ip_version> version = std::nullopt)
    {
        return udp_socket(detail::make_udp_socket_descriptor(version));
    }

    inline multicast_socket make_multicast_socket(std::optional<ip_version> version = std::nullopt)
    {
        return make_udp_socket(version);
    }
}