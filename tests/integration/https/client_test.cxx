// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0

#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ == 16
#include <cstdio>
#endif

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/io_context.hpp>

import std;
import nom.asio_comm;
import nom.core;

void require(bool condition, std::string_view message)
{
    if (!condition)
        throw std::runtime_error(std::string(message));
}

int main(int argc, char** argv)
{
    if (argc != 3)
        return 2;
    try
    {
        nom::init_logger(nom::main_logger);
        const std::string mode = argv[2];
        boost::asio::io_context io;
        auto client = std::make_unique<nom::https>(io.get_executor(), nom::url(argv[1]));
        auto params = client->params();
        params.connect_timeout_sec = 1;
        params.request_timeout_sec = 1;
        params.shutdown_timeout_sec = 1;
        params.max_pending_requests = 3;
        params.max_response_body_size = 1024;
        params.max_header_size = 1024;
        params.max_request_body_size = 16 * 1024 * 1024;
        client->update_params(params);
        nom::timer cancel_pending(io.get_executor());
        unsigned connected{}, responses{}, disconnected{}, errors{};
        std::string reason;
        nom::https::https_delegate callbacks;
        callbacks = nom::https::https_delegate{
            [&](nom::https& connection, const nom::event::on_connected&)
            {
                require(connection.status() == nom::https_status::connected, "Connected state before notification");
                ++connected;
                if (mode == "connect_callback_throw")
                    throw std::runtime_error("client callback");
                nom::http_request request{.target = "/one"};
                if (mode == "write_timeout" || mode == "write_reset")
                {
                    request.method = "POST";
                    request.body.assign(16 * 1024 * 1024, 'x');
                }
                require(connection.send(request) == 1, "First request ID");
                if (mode == "close_pending" || mode == "destroy_pending")
                    cancel_pending.arm_after(std::chrono::milliseconds{20},
                                             [&]
                                             {
                                                 if (mode == "destroy_pending")
                                                     client.reset();
                                                 else
                                                     client->close();
                                             });
                if (mode == "queue" || mode == "request_close" || mode == "peer_close" || mode == "close_queued")
                {
                    request.method = "POST";
                    request.target = "/two";
                    request.body = "abc";
                    request.keep_alive = mode != "request_close";
                    require(connection.send(request) == 2, "Second request ID");
                    request.body = "changed after send";
                    if (mode == "queue")
                    {
                        require(connection.send(nom::http_request{.method = "HEAD", .target = "/three"}) == 3,
                                "Third request ID");
                        bool rejected{};
                        try
                        {
                            connection.send(nom::http_request{});
                        }
                        catch (const std::length_error&)
                        {
                            rejected = true;
                        }
                        require(rejected, "Queue overflow must reject locally");
                    }
                }
            },
            [&](nom::https& connection, const nom::event::on_disconnected& event)
            {
                require(connection.status() == nom::https_status::disconnected,
                        "Disconnected state before notification");
                ++disconnected;
                reason = event.reason;
            },
            [&](nom::https& connection, const nom::event::network_error& event)
            {
                require(connection.status() == nom::https_status::conn_failed,
                        "Connection failure state before notification");
                ++errors;
                reason = event.description + ": " + event.message;
            },
            [&](nom::https& connection, nom::https::request_id id, const nom::http_response& response)
            {
                ++responses;
                require(response.status_code == (mode == "non_2xx" ? 429u : 200u), "Response status");
                require(response.body == (mode == "queue" && id == 3 ? "" : "ok"), "Response body");
                if (mode == "queue")
                {
                    require(id == responses, "FIFO request ID");
                }
                else if (mode == "reentry" && responses == 1)
                    require(connection.send(nom::http_request{.target = "/two"}) == 2, "Re-entry ID");
                else if (mode == "reconnect" && responses == 1)
                    connection.connect(callbacks);
                else if (mode == "destroy")
                    client.reset();
                else if (mode == "close" || mode == "close_queued")
                    connection.close();
                else if (mode == "callback_throw")
                    throw std::runtime_error("client callback");
                if (mode == "peer_close" || (mode == "request_close" && responses == 2))
                {
                    require(connection.status() == nom::https_status::disconnecting,
                            "Nonpersistent exchange must reject new work before response delivery");
                    bool rejected{};
                    try
                    {
                        connection.send(nom::http_request{});
                    }
                    catch (const std::logic_error&)
                    {
                        rejected = true;
                    }
                    require(rejected, "No sends during internal TLS shutdown");
                }
            }};
        client->connect(callbacks);
        io.run();
        require(mode != "callback_throw" && mode != "connect_callback_throw", "Expected callback exception");
        const bool setup_failure =
            mode == "untrusted" || mode == "wrong_host" || mode == "connect_timeout" || mode == "tcp_refused";
        const bool response_failure = mode == "body_limit" || mode == "header_limit" || mode == "truncated" ||
                                      mode == "response_timeout" || mode == "write_timeout" || mode == "write_reset" ||
                                      mode == "upgrade";
        const bool silent = mode == "close" || mode == "close_queued" || mode == "destroy" || mode == "close_pending" ||
                            mode == "destroy_pending";
        const unsigned expected_responses =
            setup_failure || response_failure || mode == "close_pending" || mode == "destroy_pending" ? 0
            : mode == "queue"                                                                         ? 3
            : mode == "reentry" || mode == "reconnect" || mode == "request_close"                     ? 2
                                                                                                      : 1;
        require(responses == expected_responses, "Unexpected response count");
        require(connected == (setup_failure ? 0u : mode == "reconnect" ? 2u : 1u), "Connected event count");
        require(errors == (setup_failure ? 1u : 0u), "Network error count");
        require(disconnected == (setup_failure || silent ? 0u : 1u), "Disconnected event count");
        if (mode == "connect_timeout")
            require(reason == "HTTPS connection: timeout", "Connection deadline diagnostic");
        if (mode == "response_timeout" || mode == "write_timeout")
            require(reason == "HTTPS request 1: timeout", "Request deadline diagnostic");
        if (mode == "shutdown_stall")
            require(reason == "HTTPS shutdown: timeout", "Shutdown deadline diagnostic");
        if (mode == "tcp_refused")
            require(reason.starts_with("TCP connect: "), "TCP failure diagnostic");
        if (mode == "untrusted" || mode == "wrong_host")
            require(reason.starts_with("TLS handshake: "), "TLS failure diagnostic");
        if (mode == "write_reset")
            require(reason.starts_with("HTTP write: "), "Write failure diagnostic");
        if (mode == "body_limit" || mode == "header_limit" || mode == "truncated")
            require(reason.starts_with("HTTP read: "), "Read failure diagnostic");
        if (mode == "upgrade")
            require(reason == "HTTP response: Protocol switching is not supported", "Protocol failure diagnostic");
        if (mode == "body_limit" || mode == "header_limit")
            require(reason.find("limit") != std::string::npos, "Expected limit diagnostic");
        if (client)
            require(client->status() ==
                        (setup_failure ? nom::https_status::conn_failed : nom::https_status::disconnected),
                    "Terminal status");
        std::cout << mode << ": passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        // The application boundary reports the failure and exits without resuming event processing.
        std::cerr << error.what() << '\n';
        return 1;
    }
}
