#define TEST_SUITE_NAME AsyncSocketTestSuite

#include "test_suite.hpp"
#include <webcraft/async/async.hpp>
#include <webcraft/async/io/core.hpp>
#include <webcraft/async/io/adaptors.hpp>
#include <webcraft/async/io/socket.hpp>
#include <filesystem>
#include <sstream>

using namespace webcraft::async;
using namespace webcraft::async::io;
using namespace webcraft::async::io::adaptors;
using namespace webcraft::async::io::socket;

const std::string host = "google.com";
const uint16_t port = 80;

struct connection_results
{
    std::string response;
    int status_code;
};

connection_results get_google_results_sync();

task<connection_results> get_google_results_async(tcp_rstream rstream, tcp_wstream &wstream);

TEST_CASE(TestSocketConnection)
{
    runtime_context context;

    connection_results sync_results = get_google_results_sync();

    auto task_fn = [&]() -> task<void>
    {
        auto socket = co_await make_tcp_socket();

        co_await socket.connect({host, port});

        ASSERT_EQ(host, socket.get_remote_host()) << "Remote host should match";
        ASSERT_EQ(port, socket.get_remote_port()) << "Remote port should match";

        auto &socket_rstream = socket.get_readable_stream();
        auto &socket_wstream = socket.get_writable_stream();

        connection_results async_results = co_await get_google_results_async(socket_rstream, socket_wstream);

        ASSERT_EQ(async_results.status_code, sync_results.status_code) << "Status codes should be the same";
        ASSERT_EQ(async_results.response, sync_results.response) << "Responses should be the same";
    };

    sync_wait(task_fn());
}

// TODO: Make another test case with server running in one thread and sync client running in another

task<void> handle_server_side_async(tcp_socket &client_peer);
task<void> handle_client_side_async(tcp_socket &client);

TEST_CASE(TestSocketPubSub)
{
    runtime_context context;

    async_event signal;
    const std::string localhost = "127.0.0.1";
    const uint16_t port = 5000;

    auto listener_fn = [&]() -> task<void>
    {
        tcp_listener listener = co_await make_tcp_listener();

        co_await listener.bind({localhost, port});

        co_await listener.listen(1);

        // send the signal that server is ready for connections
        signal.set();

        tcp_socket client_peer = co_await listener.accept();
        co_await handle_server_side_async(client_peer);
    };

    auto socket_fn = [&]() -> task<void>
    {
        tcp_socket client = co_await make_tcp_socket();

        // Wait until server is set up
        co_await signal;

        co_await client.connect({localhost, port});
        ASSERT_EQ(localhost, client.get_remote_host()) << "Remote host should match";
        ASSERT_EQ(port, client.get_remote_port()) << "Remote port should match";

        co_await handle_client_side_async(client);
    };

    sync_wait(when_all(listener_fn(), socket_fn()));
}