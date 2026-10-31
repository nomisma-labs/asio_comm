// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

// GCC 16's Ubuntu snapshot needs stdio declarations before these module imports at -O2.
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ == 16
#include <cstdio>
#endif

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/io_context.hpp>

import std;
import nom.asio_comm;
import nom.spd_logger;

int main()
{
    nom::init_logger(nom::main_logger);
    boost::asio::io_context io;
    nom::web_socket client(io.get_executor(), nom::url("wss://localhost:9443/feed?one=two#local"));
    const nom::web_socket::delegate callbacks{[](nom::web_socket&, const nom::event::ws_data&) {},
                                              [](nom::web_socket&, const nom::event::on_connected&) {},
                                              [](nom::web_socket&, const nom::event::on_disconnected&) {},
                                              [](nom::web_socket&, const nom::event::network_error&) {}};
    client.params().headers = {{.name = "X-Client", .value = "library-usage"}};
    client.connect(callbacks);
    client.close();
    client.connect(callbacks);
    client.close();
    io.run();
    return client.url().scheme() == "wss" && client.url().host() == "localhost" && client.url().port() == 9443 &&
                   client.url().target() == "/feed?one=two" && nom::enum_to_string(client.status()) == "disconnected"
               ? 0
               : 1;
}
