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
        bool expected_failure = false;
        std::string_view expected_description;
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
                    expected_failure = nom::ws_status::conn_failed == client.status() &&
                                       event.description == expected_description && !event.message.empty();
                    expected_failure = !client.send("after connection failure") && expected_failure;
                    const auto failed_id = client.session_id();
                    std::cerr << event.description << ": " << event.message << '\n';
                    finish();
                    expected_failure = expected_failure && client.status() == nom::ws_status::conn_failed &&
                                       client.session_id() == failed_id;
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
            expected_failure = false;
            expected_description = "Connect/Handshake error";
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

            auto params = client.params();
            if (attempt == 0)
            {
                // Replace an outstanding attempt before any completion handler runs.
                params.headers = {{.name = "Authorization", .value = "Bearer abandoned"}};
                client.update_params(params);
                client.connect(callbacks);
                client.close();
                params.headers = {{.name = "Authorization", .value = "Bearer updated"},
                                  {.name = "X-Attempt", .value = "first"},
                                  {.name = "User-Agent", .value = "headers-test/1"}};
                params.agent_name = "overridden-by-header";
                client.update_params(params);
                client.connect(nom::web_socket::delegate{[&](nom::web_socket&, const auto&)
                                                         {
                                                             std::cerr << "Callback replacement did not take effect\n";
                                                             finish();
                                                         }});
                client.update_delegate(callbacks);
            }
            else if (attempt == 1)
            {
                params.headers = {{.name = "Authorization", .value = "Bearer second"},
                                  {.name = "X-Attempt", .value = "second"}};
                params.agent_name = "headers-test/2";
                client.update_params(params);
                client.connect(client.callbacks());
            }
            else if (attempt == 2)
            {
                // Headers and agent persist; the peer also checks repeated configured outbound pings.
                params.active_ping = nom::ws_params::client_ping_probe{};
                params.active_ping->ping_period_sec = 1;
                params.receive_watchdog = nom::ws_params::receive_data_watchdog{.timeout_sec = 1};
                client.update_params(params);
                client.connect(client.callbacks());
            }
            else
            {
                client.update_params({}); // Explicitly restore defaults, including empty headers.
                client.connect(client.callbacks());
            }

            const auto configured_agent = client.params().agent_name;
            params = client.params();
            params.agent_name = "rejected-active-update";
            try
            {
                client.update_params(params);
                std::cerr << "Parameter update was accepted while connecting\n";
                return 1;
            }
            catch (const std::invalid_argument&)
            {
                throw;
            }
            catch (const std::logic_error&)
            {
            }
            if (client.params().agent_name != configured_agent)
                return 1;

            io.run();
            if (attempt == 3)
            {
                if (!expected_failure || network_errors != 1 || connected != 0 || accepted)
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
