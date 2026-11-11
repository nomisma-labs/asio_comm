// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0

// GCC 16's Ubuntu snapshot needs stdio declarations before these module imports at -O2.
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ == 16
#include <cstdio>
#endif

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/io_context.hpp>

import std;
import nom.asio_comm;
import nom.core;

int main()
{
    nom::init_logger(nom::main_logger);
    boost::asio::io_context io;
    nom::web_socket client(io.get_executor(), nom::url("wss://localhost:9443/feed?one=two#local"));
    const nom::web_socket::ws_delegate callbacks{[](nom::web_socket&, const nom::event::ws_data&) {},
                                                 [](nom::web_socket&, const nom::event::on_connected&) {},
                                                 [](nom::web_socket&, const nom::event::on_disconnected&) {},
                                                 [](nom::web_socket&, const nom::event::network_error&) {}};
    auto params = client.params();
    params.headers = {{.name = "X-Client", .value = "library-usage"}};
    if (!params.validate())
        return 1;
    client.update_params(params);
    client.connect(callbacks);
    client.close();
    client.connect(callbacks);
    client.close();
    nom::timer deadline(io.get_executor());
    bool timer_called = false;
    deadline.arm_after(std::chrono::milliseconds{0}, [&] { timer_called = true; });
    nom::https http_client(io.get_executor(), nom::url("https://localhost:9443/info"));
    if (!http_client.params().validate() || !nom::http_request{}.validate())
        return 1;
    http_client.close();
    io.run();
    return timer_called && http_client.status() == nom::https_status::init && client.url().scheme() == "wss" &&
                   client.url().host() == "localhost" && client.url().port() == 9443 &&
                   client.url().target() == "/feed?one=two" && nom::enum_to_string(client.status()) == "conn_failed"
               ? 0
               : 1;
}
