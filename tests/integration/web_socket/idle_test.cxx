// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ == 16
#include <cstdio>
#endif

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/system/error_code.hpp>

import std;
import nom.asio_comm;
import nom.spd_logger;

int main(int argc, char** argv)
{
    if (argc != 3)
        return 1;
    try
    {
        nom::init_logger(nom::main_logger);
        const std::string_view mode(argv[2]);
        const bool expect_idle_close = mode == "idle_only" || mode == "idle_with_ping";
        if (!expect_idle_close && mode != "disabled" && mode != "disabled_with_ping" && mode != "independent")
            return 1;

        boost::asio::io_context io;
        nom::web_socket client(io.get_executor(), nom::url(argv[1]));
        auto params = client.params();
        if (expect_idle_close)
            params.receive_watchdog = nom::ws_params::receive_data_watchdog{.timeout_sec = 1};
        else if (mode == "independent")
            params.receive_watchdog = nom::ws_params::receive_data_watchdog{.timeout_sec = 5};
        if (mode != "idle_only" && mode != "disabled")
            params.active_ping = nom::ws_params::client_ping_probe{
                .ping_period_sec = mode == "idle_with_ping" ? 5u : 1u, .pong_timeout_sec = 1};
        client.update_params(params);

        boost::asio::steady_timer deadline(io);
        nom::network_timestamp connected_at;
        unsigned connected = 0;
        unsigned disconnected = 0;
        bool accepted = false;
        bool failed = false;
        const auto finish = [&]
        {
            client.close();
            deadline.cancel();
        };
        const auto fail = [&](std::string_view reason)
        {
            std::cerr << reason << '\n';
            failed = true;
            finish();
        };
        const nom::web_socket::delegate callbacks{
            [&](nom::web_socket&, const auto& event)
            {
                using event_type = std::remove_cvref_t<decltype(event)>;
                if constexpr (std::same_as<event_type, nom::event::on_connected>)
                {
                    ++connected;
                    connected_at = event.ts;
                }
                else if constexpr (std::same_as<event_type, nom::event::on_disconnected>)
                {
                    ++disconnected;
                    const auto elapsed = event.ts - connected_at;
                    if (!expect_idle_close || client.status() != nom::ws_status::disconnected ||
                        elapsed < std::chrono::seconds(1) || elapsed >= std::chrono::seconds(5))
                        fail("Unexpected disconnection or idle timing");
                    else
                        finish();
                }
                else if constexpr (std::same_as<event_type, nom::event::ws_data>)
                {
                    if (expect_idle_close || event.sv_data() != "accepted")
                        fail("Unexpected message from idle test peer");
                    else
                    {
                        accepted = true;
                        finish();
                    }
                }
                else if constexpr (std::same_as<event_type, nom::event::network_error>)
                {
                    fail(event.description);
                }
            }};
        deadline.expires_after(std::chrono::seconds(10));
        deadline.async_wait(
            [&](const boost::system::error_code& error)
            {
                if (!error)
                    fail("Idle test did not finish");
            });
        client.connect(callbacks);
        io.run();
        return !failed && connected == 1 && (expect_idle_close ? disconnected == 1 : accepted && disconnected == 0) ? 0
                                                                                                                    : 1;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
