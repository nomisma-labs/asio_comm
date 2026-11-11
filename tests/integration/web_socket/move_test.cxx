// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0

#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ == 16
#include <cstdio>
#endif

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>

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
        boost::asio::io_context io;
        const nom::url address(argv[1]);
        const std::string_view mode(argv[2]);
        const bool assign = mode == "assign" || mode == "connected_assign";
        const bool after_connected = mode == "connected" || mode == "connected_assign";
        nom::web_socket source(io.get_executor(), address);
        auto params = source.params();
        params.headers = {{.name = "X-Move", .value = "source"}};
        params.active_ping = nom::ws_params::client_ping_probe{};
        params.active_ping->ping_period_sec = 1;
        source.update_params(params);
        std::optional<nom::web_socket> destination;
        nom::web_socket* expected_owner = &source;
        nom::timer deadline(io.get_executor());
        unsigned connected = 0;
        unsigned received = 0;
        bool failed = false;
        const auto finish = [&]
        {
            source.close();
            if (destination)
                destination->close();
            deadline.cancel();
        };
        const auto fail = [&](std::string_view message)
        {
            std::cerr << message << '\n';
            failed = true;
            finish();
        };
        const auto transfer = [&]
        {
            const auto source_id = source.session_id();
            if (assign)
            {
                destination.emplace(io.get_executor(), address);
                destination->connect(nom::web_socket::ws_delegate{
                    [&](nom::web_socket&, const auto&) { fail("Replaced session notified its old owner"); }});
                const auto replaced_id = destination->session_id();
                *destination = std::move(source);
                if (source.status() != nom::ws_status::conn_failed || source.session_id() != replaced_id)
                    fail("Move assignment did not leave a valid canceled replacement in the source");
            }
            else
            {
                destination.emplace(std::move(source));
                if (source.status() != nom::ws_status::init || source.session_id() == source_id)
                    fail("Move construction did not leave a fresh initial session in the source");
            }
            expected_owner = &*destination;
            if (destination->session_id() != source_id || destination->params().headers.size() != 1 ||
                destination->params().headers.front().value != "source")
                fail("Move lost session identity or parameters");
        };
        const nom::web_socket::ws_delegate callbacks{
            [&](nom::web_socket& connection, const auto& event)
            {
                if (&connection != expected_owner)
                {
                    fail("Callback reached the wrapper at its old address");
                    return;
                }
                using event_type = std::remove_cvref_t<decltype(event)>;
                if constexpr (std::same_as<event_type, nom::event::on_connected>)
                {
                    ++connected;
                    if (after_connected)
                    {
                        // Move after this callback returns, with the read and heartbeat waits outstanding.
                        boost::asio::post(io,
                                          [&]
                                          {
                                              transfer();
                                              if (!failed && !destination->send("moved"))
                                                  fail("Send failed after moving a connected session");
                                          });
                    }
                    else if (!connection.send("moved"))
                        fail("Send failed after moving a connecting session");
                }
                else if constexpr (std::same_as<event_type, nom::event::ws_data>)
                {
                    ++received;
                    if (event.sv_data() != "accepted")
                        fail("Unexpected peer payload");
                    finish();
                }
                else if constexpr (std::same_as<event_type, nom::event::network_error>)
                    fail(event.description);
                else if constexpr (std::same_as<event_type, nom::event::on_disconnected>)
                    fail("Disconnected before receiving the peer payload");
            }};
        deadline.arm_after(std::chrono::seconds(5), [&] { fail("Timed out waiting for the moved session"); });
        source.connect(callbacks);
        if (!after_connected)
            transfer();
        io.run();
        return !failed && connected == 1 && received == 1 ? 0 : 1;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
