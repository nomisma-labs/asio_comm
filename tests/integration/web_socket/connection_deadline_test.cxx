// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0

#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ == 16
#include <cstdio>
#endif

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/io_context.hpp>
#if defined(__linux__)
#include <dlfcn.h>
#include <netdb.h>
#endif

import std;
import nom.asio_comm;
import nom.core;

#if defined(__linux__)
// Test-executable interposition keeps the real resolver worker busy beyond the deadline.
// A long local alias also exercises SNI setup failure after TCP connects.
// No external DNS or production-code hook is needed.
extern "C" int getaddrinfo(const char* name, const char* service, const addrinfo* hints, addrinfo** result)
{
    if (name && std::string_view(name) == "slow-dns.test")
    {
        std::this_thread::sleep_for(std::chrono::milliseconds{1500});
        *result = nullptr;
        return EAI_AGAIN;
    }
    if (name && std::string_view(name).starts_with("long-sni-") && std::string_view(name).size() == 256)
        name = "localhost";
    using resolver = int (*)(const char*, const char*, const addrinfo*, addrinfo**);
    static const auto resolve = reinterpret_cast<resolver>(dlsym(RTLD_NEXT, "getaddrinfo"));
    return resolve ? resolve(name, service, hints, result) : EAI_FAIL;
}
#endif

void require(bool condition, std::string_view message)
{
    if (!condition)
        throw std::runtime_error(std::string(message));
}

int main(int argc, char** argv)
{
    if (argc != 3)
        return 2;
    try
    {
        nom::init_logger(nom::main_logger);
        const std::string_view mode = argv[2];
        boost::asio::io_context io;
        auto client = std::make_unique<nom::web_socket>(io.get_executor(), nom::url(argv[1]));
        auto params = client->params();
        params.async_operation_timeout_sec = 1;
        client->update_params(params);
        nom::timer action(io.get_executor()), limit(io.get_executor());
        limit.arm_after(std::chrono::seconds{4}, [] { throw std::runtime_error("Test deadline exceeded"); });
        unsigned connected{}, received{}, errors{};
        const auto started = std::chrono::steady_clock::now();
        nom::web_socket::ws_delegate callbacks;
        callbacks = nom::web_socket::ws_delegate{
            [&](nom::web_socket& connection, const nom::event::ws_data& event)
            {
                require(&connection == client.get(), "Data callback owner");
                require(std::string_view(event.data.data(), event.data.size()) == "finish", "Expected finish data");
                ++received;
                connection.close();
                limit.cancel();
            },
            [&](nom::web_socket& connection, const nom::event::on_connected&)
            {
                require(&connection == client.get(), "Connected callback owner");
                require(mode == "success" || mode == "reconnect", "Unexpected successful connection");
                require(connection.status() == nom::ws_status::connected, "Connected status before notification");
                ++connected;
            },
            [&](nom::web_socket&, const nom::event::on_disconnected&)
            { throw std::runtime_error("Setup failure or silent close must not emit on_disconnected"); },
            [&](nom::web_socket& connection, const nom::event::network_error& event)
            {
                require(&connection == client.get(), "Error callback owner after move");
                require(connection.status() == nom::ws_status::conn_failed, "Failure status before notification");
                require(++errors == 1, "Duplicate connection failure");
                if (mode == "reject")
                    require(event.description == "Connect/Handshake error", "Expected rejected upgrade");
                else if (mode == "tls_error")
                    require(event.description == "Connect/SSL handshake error", "Expected TLS verification failure");
                else if (mode == "sni_error")
                    require(event.description == "Connect/SSL error", "Expected SNI setup failure");
                else
                {
                    require(event.description == "Connection timeout", "Expected connection deadline event");
                    require(event.message == "Connection deadline exceeded after 1 seconds", "Timeout diagnostic");
                    const auto elapsed = std::chrono::steady_clock::now() - started;
                    require(elapsed >= std::chrono::milliseconds{900}, "Connection deadline fired early");
                    if (mode == "combined_timeout")
                        require(elapsed < std::chrono::milliseconds{1400}, "Deadline restarted between stages");
                }
                if (mode == "reconnect")
                    connection.connect(callbacks);
                else
                {
                    limit.cancel();
                    if (mode == "reject" || mode == "tls_error" || mode == "sni_error")
                    {
                        // Keep both the client and callback alive. The peer must observe transport closure
                        // before acknowledging on stdin; neither close() nor destruction can mask a leak.
                        std::string acknowledgement;
                        require(static_cast<bool>(std::getline(std::cin, acknowledgement)) &&
                                    acknowledgement == "peer_closed",
                                "Peer must observe closure before the error callback returns");
                    }
                }
            }};
        client->connect(callbacks);
        if (mode == "close" || mode == "destroy" || mode == "dns_close" || mode == "dns_destroy")
            action.arm_after(std::chrono::milliseconds{150},
                             [&]
                             {
                                 if (mode == "destroy" || mode == "dns_destroy")
                                     client.reset();
                                 else
                                     client->close();
                                 limit.cancel();
                             });
        else if (mode == "move" || mode == "move_assign")
            action.arm_after(std::chrono::milliseconds{150},
                             [&]
                             {
                                 const auto id = client->session_id();
                                 std::unique_ptr<nom::web_socket> destination;
                                 if (mode == "move")
                                     destination = std::make_unique<nom::web_socket>(std::move(*client));
                                 else
                                 {
                                     destination = std::make_unique<nom::web_socket>(io.get_executor(), client->url());
                                     *destination = std::move(*client);
                                 }
                                 client = std::move(destination);
                                 require(client->session_id() == id, "Move preserves session and deadline");
                             });
        else if (mode == "late_close")
        {
            // Let expiry become ready before canceling, without dispatching it.
            std::this_thread::sleep_for(std::chrono::milliseconds{1200});
            client->close();
            limit.cancel();
        }
        io.run();
        const bool silent = mode == "close" || mode == "destroy" || mode == "dns_close" || mode == "dns_destroy" ||
                            mode == "late_close";
        const bool succeeded = mode == "success" || mode == "reconnect";
        require(errors == (silent || mode == "success" ? 0u : 1u), "Network error count");
        require(connected == (succeeded ? 1u : 0u) && received == connected, "Connected/data event counts");
        if (client)
            require(client->status() == (succeeded ? nom::ws_status::disconnected : nom::ws_status::conn_failed),
                    "Final session status");
        if (mode == "close" || mode == "destroy" || mode == "reject")
            require(std::chrono::steady_clock::now() - started < std::chrono::milliseconds{800},
                    "Canceled setup left its deadline outstanding");
        std::cout << mode << ": passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
