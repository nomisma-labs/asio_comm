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
        nom::web_socket client(io.get_executor(), nom::url(argv[1]));
        client.params().enable_outbound_ping = true;
        boost::asio::steady_timer deadline(io);
        unsigned connected = 0;
        unsigned disconnected = 0;
        bool saw_disconnecting = false;
        bool failed = false;
        const auto fail = [&](std::string_view reason)
        {
            std::cerr << reason << '\n';
            failed = true;
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
                    if (!client.is_connected() || !client.send("trigger"))
                    {
                        fail("Expected a connected session before the peer reset");
                        return;
                    }

                    // The peer resets TCP after receiving the trigger. Keep writes inside this
                    // callback so a read completion cannot handle that reset before the write fails.
                    const std::string payload(64 * 1024, 'x');
                    bool write_failed = false;
                    for (unsigned attempt = 0; attempt < 32; ++attempt)
                    {
                        if (!client.send(payload))
                        {
                            write_failed = true;
                            break;
                        }
                    }
                    saw_disconnecting = client.status() == nom::ws_status::disconnecting;
                    if (!write_failed || !saw_disconnecting || client.is_connected())
                    {
                        fail("A write failure must enter disconnecting before the close callback");
                        return;
                    }
                    if (client.send("after close started"))
                    {
                        fail("send must reject a disconnecting session");
                        return;
                    }
                    if (nom::ws_status::disconnecting != client.status())
                        fail("Rejected sends must leave the session disconnecting");
                }
                else if constexpr (std::same_as<event_type, nom::event::on_disconnected>)
                {
                    ++disconnected;
                    if (!saw_disconnecting || client.status() != nom::ws_status::disconnected)
                    {
                        fail("Close completion must transition disconnecting to disconnected");
                        return;
                    }
                    // Keep the session attached while remaining completions drain, so duplicate
                    // terminal events remain observable. No receive or timer work should start.
                    deadline.cancel();
                }
                else if constexpr (std::same_as<event_type, nom::event::network_error>)
                {
                    fail(event.description);
                }
                else if constexpr (std::same_as<event_type, nom::event::ws_data>)
                {
                    fail("Unexpected data from the reset peer");
                }
            }};

        deadline.expires_after(std::chrono::seconds(5));
        deadline.async_wait(
            [&](const boost::system::error_code& error)
            {
                if (!error)
                    fail("Timed out waiting for disconnect completion");
            });
        client.connect(callbacks);
        io.run();
        return !failed && saw_disconnecting && connected == 1 && disconnected == 1 ? 0 : 1;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
