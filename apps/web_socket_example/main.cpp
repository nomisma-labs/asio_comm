// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

// GCC 16's Ubuntu snapshot ICEs in fortified snprintf at -O2 after import std.
// As in base's consumer test, make the stdio declarations visible before imports.
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ == 16
#include <cstdio>
#endif

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/io_context.hpp>

import std;
import nom.asio_comm;
import nom.spd_logger;

#include "config.h"

int main(int argc, char** argv)
{
    if (argc > 3 || (argc > 1 && std::string_view(argv[1]) == "--help"))
    {
        std::cout << "Usage: web_socket_example [wss://HOST[:PORT]/PATH [MESSAGE]]\n"
                     "With no arguments, send hello to wss://ws.postman-echo.com/raw.\n"
                     "Send an optional text message, print the first reply, then close.\n"
                     "With a URL but no MESSAGE, wait for the server's first message.\n";
        return argc == 2 && std::string_view(argv[1]) == "--help" ? 0 : 1;
    }

    try
    {
        const auto config = parse_arguments(argc, argv);
        if (const auto validation = config.validate(); !validation)
        {
            std::cerr << validation.error() << '\n';
            return 1;
        }
        nom::init_logger(nom::main_logger);
        boost::asio::io_context io;
        // This generic probe sends at most one application message and closes after the first reply.
        // It has no exchange subscription/count/rate policy; consult the selected endpoint's API docs.
        nom::web_socket client(io.get_executor(), nom::url(config.endpoint));
        int result = 1;

        // One io_context thread serializes the client and its callbacks.
        client.connect(nom::web_socket::ws_delegate(
            [&](nom::web_socket&, const nom::event::ws_data& event)
            {
                std::cout << "Received: " << event.sv_data() << '\n';
                result = 0;
                client.close();
            },
            [&](nom::web_socket& connection, const nom::event::on_connected&)
            {
                std::cout << "Connected: " << connection.session_id() << '\n';
                if (config.message && !connection.send(*config.message))
                {
                    std::cerr << "Send failed\n";
                    client.close();
                }
            },
            [&](nom::web_socket&, const nom::event::on_disconnected& event)
            {
                std::cerr << "Disconnected before receiving a message: " << event.reason << '\n';
                client.close();
            },
            [&](nom::web_socket&, const nom::event::network_error& event)
            {
                std::cerr << event.description << ": " << event.message << '\n';
                client.close();
            }));

        io.run(); // Drain canceled handlers too; no final event is required after close().
        nom::flush_logger(nom::main_logger);
        return result;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
