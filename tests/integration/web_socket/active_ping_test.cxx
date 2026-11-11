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

int main(int argc, char** argv)
{
    if (argc != 3)
        return 1;
    try
    {
        nom::init_logger(nom::main_logger);
        const std::string_view mode(argv[2]);
        const bool expect_timeout = mode == "silent" || mode == "traffic" || mode == "move" || mode == "reconnect";
        const bool expect_send_timeout = mode == "send_timeout";
        const bool expect_unexpected_pong = mode == "unsolicited" || mode == "duplicate";
        const bool expect_peer_close = mode.starts_with("peer_close") || mode.starts_with("read_error");
        const bool long_interval = mode == "peer_close_interval" || mode == "read_error_interval";
        const bool long_deadline = expect_peer_close || mode == "close_pending";
        boost::asio::io_context io;
        nom::web_socket client(io.get_executor(), nom::url(argv[1]));
        auto params = client.params();
        if (mode != "disabled")
            params.active_ping = nom::ws_params::client_ping_probe{.ping_period_sec = long_interval ? 10u : 1u,
                                                                   .pong_timeout_sec = long_deadline ? 10u : 2u};
        if (mode == "traffic")
        {
            params.passive_ping = nom::ws_params::server_ping_watchdog{.ping_timeout_sec = 2};
        }
        if (mode == "destroy_pending")
            params.passive_ping = nom::ws_params::server_ping_watchdog{.ping_timeout_sec = 10};
        if (expect_send_timeout || mode == "send_unblocked")
        {
            params.async_operation_timeout_sec = 2;
            params.socket_send_buf_size = 1024;
        }
        client.update_params(params);

        std::optional<nom::web_socket> moved;
        nom::web_socket* owner = &client;
        nom::timer deadline(io.get_executor());
        nom::network_timestamp connected_at{};
        unsigned connected = 0;
        unsigned disconnected = 0;
        unsigned received = 0;
        bool finished = false;
        bool failed = false;
        const auto stop = [&]
        {
            owner->close();
            deadline.cancel();
        };
        const auto fail = [&](std::string_view reason)
        {
            std::cerr << reason << '\n';
            failed = true;
            stop();
        };
        nom::web_socket::ws_delegate callbacks;
        callbacks = nom::web_socket::ws_delegate{
            [&](nom::web_socket& connection, const auto& event)
            {
                if (&connection != owner)
                {
                    fail("Callback reached the wrong owner");
                    return;
                }
                using event_type = std::remove_cvref_t<decltype(event)>;
                if constexpr (std::same_as<event_type, nom::event::on_connected>)
                {
                    ++connected;
                    connected_at = event.ts;
                    if (mode == "move" || mode == "destroy_pending")
                    {
                        moved.emplace(std::move(client));
                        owner = &*moved;
                    }
                }
                else if constexpr (std::same_as<event_type, nom::event::on_disconnected>)
                {
                    ++disconnected;
                    if (disconnected != 1 || connection.status() != nom::ws_status::disconnected)
                    {
                        fail("Expected one notification after entering disconnected");
                        return;
                    }
                    if (expect_timeout)
                    {
                        const auto elapsed = event.ts - connected_at;
                        if (event.reason != "Active Pong timeout: no Pong within 2 seconds" ||
                            elapsed < std::chrono::seconds(3) || elapsed >= std::chrono::seconds(7) ||
                            (mode == "traffic" && received < 5))
                        {
                            fail("Wrong active Pong timeout reason or deadline");
                            return;
                        }
                        if (mode == "reconnect")
                        {
                            client.connect(callbacks);
                            return;
                        }
                    }
                    else if (expect_send_timeout)
                    {
                        const auto elapsed = event.ts - connected_at;
                        if (event.reason != "Active Ping send timeout: write did not complete within 2 seconds" ||
                            elapsed < std::chrono::seconds(3) || elapsed >= std::chrono::seconds(7))
                        {
                            fail("Wrong active Ping send timeout reason or deadline");
                            return;
                        }
                    }
                    else if (expect_unexpected_pong)
                    {
                        if (event.reason != "Active Pong received in unexpected status 1")
                        {
                            fail("Expected Pong in waiting_to_ping to fail the probe");
                            return;
                        }
                    }
                    else if (!expect_peer_close || event.reason.starts_with("Active"))
                    {
                        fail("Unexpected active Ping disconnection");
                        return;
                    }
                    // Leave the session alive: io.run() must drain without a close() hiding a retained timer.
                    deadline.cancel();
                }
                else if constexpr (std::same_as<event_type, nom::event::ws_data>)
                {
                    ++received;
                    if (event.sv_data() == "finish")
                    {
                        finished = true;
                        if (mode == "destroy_pending")
                        {
                            moved.reset();
                            owner = &client;
                            deadline.cancel();
                        }
                        else
                            stop();
                    }
                    else if (event.sv_data() == "stall" && mode == "queued")
                        std::this_thread::sleep_for(std::chrono::milliseconds(2200));
                    else if (event.sv_data() == "late" && (mode == "late" || mode == "late_rearm"))
                        std::this_thread::sleep_for(std::chrono::milliseconds(2200));
                    else if (event.sv_data() == "after_pong" && mode == "late_rearm")
                        // The Pong has replaced its wait with the interval. Delay past that new deadline too.
                        std::this_thread::sleep_for(std::chrono::milliseconds(1200));
                    else if (event.sv_data() != "data" || mode != "traffic")
                        fail("Unexpected active Ping test data");
                }
                else if constexpr (std::same_as<event_type, nom::event::network_error>)
                    fail(event.description);
            }};
        deadline.arm_after(std::chrono::seconds(12), [&] { fail("Active Ping test did not finish"); });
        client.connect(callbacks);
        if (mode == "send_completion_delay")
        {
            while (connected == 0 && !failed)
                io.run_one();
            // This peer waits for our Ping. Dispatch its interval, then pause before the queued
            // write completion: the Pong timeout must start only after that completion runs.
            const auto first_ping_at = connected_at + std::chrono::seconds(1);
            do
            {
                if (io.run_one() == 0)
                    break;
            } while (std::chrono::steady_clock::now() < first_ping_at && !failed);
            std::this_thread::sleep_for(std::chrono::milliseconds(2200));
        }
        io.run();
        const unsigned expected_connected = mode == "reconnect" ? 2u : 1u;
        const unsigned expected_disconnected =
            expect_timeout || expect_send_timeout || expect_peer_close || expect_unexpected_pong ? 1u : 0u;
        const bool needs_finish = !expected_disconnected || mode == "reconnect";
        return !failed && connected == expected_connected && disconnected == expected_disconnected &&
                       (!needs_finish || finished)
                   ? 0
                   : 1;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
