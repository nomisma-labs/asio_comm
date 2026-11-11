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
        const bool expect_timeout = mode == "silent" || mode == "traffic" || mode == "refresh" || mode == "reconnect" ||
                                    mode == "move" || mode == "slow_handler";
        const bool expect_peer_close = mode == "peer_close" || mode == "read_error";
        if (!expect_timeout && !expect_peer_close && mode != "disabled" && mode != "close_connected" &&
            mode != "close_after_ping" && mode != "late_handshake")
            return 1;

        boost::asio::io_context io;
        nom::web_socket client(io.get_executor(), nom::url(argv[1]));
        auto params = client.params();
        if (mode != "disabled")
            params.passive_ping = nom::ws_params::server_ping_watchdog{
                .ping_timeout_sec = expect_timeout || mode == "late_handshake" ? 2u : 10u};
        if (mode == "traffic")
        {
            params.active_ping = nom::ws_params::client_ping_probe{.ping_period_sec = 1, .pong_timeout_sec = 1};
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
                    if (mode == "move")
                    {
                        moved.emplace(std::move(client));
                        owner = &*moved;
                    }
                    if (mode == "slow_handler")
                        std::this_thread::sleep_for(std::chrono::milliseconds(1200));
                    if (mode == "close_connected")
                    {
                        finished = true;
                        stop();
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
                        const auto minimum =
                            mode == "refresh" ? std::chrono::milliseconds(3500) : std::chrono::milliseconds(2000);
                        const auto maximum =
                            mode == "slow_handler" ? std::chrono::milliseconds(2800) : std::chrono::milliseconds(6500);
                        if (event.reason != "Passive Ping timeout: no server Ping within 2 seconds" ||
                            elapsed < minimum || elapsed >= maximum || (mode == "traffic" && received < 6))
                        {
                            fail("Wrong passive Ping timeout reason or deadline");
                            return;
                        }
                        if (mode == "reconnect")
                        {
                            client.connect(callbacks);
                            return;
                        }
                    }
                    else if (!expect_peer_close || event.reason.starts_with("Passive Ping"))
                    {
                        fail("Unexpected disconnection");
                        return;
                    }
                    // Keep the terminal session alive while completions drain; do not mask missing cancellation.
                    deadline.cancel();
                }
                else if constexpr (std::same_as<event_type, nom::event::ws_data>)
                {
                    ++received;
                    if (event.sv_data() == "finish")
                    {
                        finished = true;
                        stop();
                    }
                    else if (event.sv_data() != "data" || mode != "traffic")
                        fail("Unexpected data from passive Ping peer");
                }
                else if constexpr (std::same_as<event_type, nom::event::network_error>)
                    fail(event.description);
            }};
        deadline.arm_after(std::chrono::seconds(8), [&] { fail("Passive Ping test did not finish"); });
        client.connect(callbacks);
        io.run();
        const unsigned expected_connected = mode == "reconnect" ? 2u : 1u;
        const unsigned expected_disconnected = expect_timeout || expect_peer_close ? 1u : 0u;
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
