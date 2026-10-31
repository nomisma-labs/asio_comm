// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

// GCC 16's Ubuntu snapshot needs stdio declarations before these module imports at -O2.
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
    if (argc != 2)
        return 1;

    try
    {
        nom::init_logger(nom::main_logger);
        boost::asio::io_context io;
        const nom::url address(argv[1]);
        nom::web_socket client(io.get_executor(), address.host(), address.port(), address.target());
        boost::asio::steady_timer deadline(io);
        bool accepted = false;
        unsigned connected = 0;
        unsigned network_errors = 0;
        bool setup_failure = false;
        const auto finish = [&]
        {
            client.close();
            deadline.cancel();
        };
        const nom::web_socket::delegate callbacks{
            [&](nom::web_socket&, const auto& event)
            {
                using event_type = std::remove_cvref_t<decltype(event)>;
                if constexpr (std::same_as<event_type, nom::event::on_connected>)
                {
                    ++connected;
                }
                else if constexpr (std::same_as<event_type, nom::event::ws_data>)
                {
                    accepted = event.sv_data() == "accepted";
                    finish();
                }
                else if constexpr (std::same_as<event_type, nom::event::network_error>)
                {
                    ++network_errors;
                    setup_failure = nom::ws_status::conn_failed == client.status() &&
                                    event.description == "WebSocket handshake setup error" && !event.message.empty();
                    std::cerr << event.description << ": " << event.message << '\n';
                    finish();
                }
                else if constexpr (std::same_as<event_type, nom::event::on_disconnected>)
                {
                    std::cerr << "Disconnected before header validation: " << event.reason << '\n';
                    finish();
                }
            }};

        for (unsigned attempt = 0; attempt < 5; ++attempt)
        {
            io.restart();
            accepted = false;
            connected = 0;
            network_errors = 0;
            setup_failure = false;
            deadline.expires_after(std::chrono::seconds(5));
            deadline.async_wait(
                [&](const boost::system::error_code& error)
                {
                    if (!error)
                    {
                        std::cerr << "Timed out waiting for header validation\n";
                        finish();
                    }
                });

            if (attempt == 0)
            {
                // Replace an outstanding attempt before any completion handler runs.
                client.params().headers = {{.name = "Authorization", .value = "Bearer abandoned"}};
                client.connect(callbacks);
                client.params().headers = {{.name = "Authorization", .value = "Bearer first"},
                                           {.name = "X-Attempt", .value = "first"},
                                           {.name = "User-Agent", .value = "headers-test/1"}};
                client.params().agent_name = "overridden-by-header";
                client.connect(callbacks);
                // The handshake reads live params, including edits after connect() returns.
                client.params().headers.front().value = "Bearer updated";
            }
            else if (attempt == 1)
            {
                client.params().headers = {{.name = "Authorization", .value = "Bearer second"},
                                           {.name = "X-Attempt", .value = "second"}};
                client.connect(callbacks);
                client.params().agent_name = "headers-test/2";
            }
            else if (attempt == 2)
            {
                // Headers and agent persist; the peer also checks repeated configured outbound pings.
                client.connect(callbacks);
                client.params().enable_outbound_ping = true;
                client.params().ping_interval_sec = 1;
            }
            else if (attempt == 3)
            {
                // Beast throws while constructing this header after the TLS handshake completes.
                client.params().headers = {{.name = "X-Oversized", .value = std::string(65536, 'x')}};
                client.connect(callbacks);
            }
            else
            {
                client.params() = {}; // Explicitly restore defaults, including empty headers.
                client.connect(callbacks);
            }

            io.run();
            if (attempt == 3)
            {
                if (!setup_failure || network_errors != 1 || connected != 0 || accepted)
                    return 1;
            }
            else if (!accepted || connected != 1 || network_errors != 0)
                return 1;
        }
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
