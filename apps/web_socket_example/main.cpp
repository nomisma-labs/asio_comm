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
        nom::init_logger(nom::main_logger);
        boost::asio::io_context io;
        const char* endpoint = argc > 1 ? argv[1] : "wss://ws.postman-echo.com/raw";
        const char* message = argc == 1 ? "hello" : (argc == 3 ? argv[2] : nullptr);
        nom::web_socket client(io.get_executor(), nom::url(endpoint));
        int result = 1;

        // One io_context thread serializes the client and its callbacks.
        client.connect(nom::web_socket::delegate{
            [&](nom::web_socket& connection, const auto& event)
            {
                using event_type = std::remove_cvref_t<decltype(event)>;
                if constexpr (std::same_as<event_type, nom::event::on_connected>)
                {
                    std::cout << "Connected: " << connection.session_id() << '\n';
                    if (message && !connection.send(message))
                    {
                        std::cerr << "Send failed\n";
                        client.close();
                    }
                }
                else if constexpr (std::same_as<event_type, nom::event::ws_data>)
                {
                    std::cout << "Received: " << event.sv_data() << '\n';
                    result = 0;
                    client.close();
                }
                else if constexpr (std::same_as<event_type, nom::event::on_disconnected>)
                {
                    std::cerr << "Disconnected before receiving a message: " << event.reason << '\n';
                    client.close();
                }
                else if constexpr (std::same_as<event_type, nom::event::network_error>)
                {
                    std::cerr << event.description << ": " << event.message << '\n';
                    client.close();
                }
            }});

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
