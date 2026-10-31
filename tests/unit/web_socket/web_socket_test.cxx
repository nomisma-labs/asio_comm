// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

#include <catch2/catch_test_macros.hpp>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/io_context.hpp>

import std;
import nom.asio_comm;
import nom.spd_logger;

TEST_CASE("WebSocket uses the base URL and enum utilities")
{
    boost::asio::io_context io;
    nom::url address("wss", "localhost", 9443, "/feed%2Fone?topic=two");
    nom::web_socket client(io.get_executor(), address);
    address = nom::url("wss://other.example/"); // The client owns its endpoint.
    CHECK(client.url().scheme() == "wss");
    CHECK(client.url().host() == "localhost");
    CHECK(client.url().port() == 9443);
    CHECK(client.url().target() == "/feed%2Fone?topic=two");
    CHECK(client.status() == nom::ws_status::init);
    CHECK(nom::enum_to_string(client.status()) == "init");
    CHECK(nom::enum_to_string(static_cast<nom::ws_status>(255)) == "ws_status::<unknown>");
    CHECK_THROWS_AS(nom::web_socket(io.get_executor(), nom::url("https://localhost/")), std::invalid_argument);
    STATIC_REQUIRE_FALSE(std::is_move_constructible_v<nom::web_socket>);
    STATIC_REQUIRE_FALSE(std::is_copy_constructible_v<nom::web_socket>);
}

TEST_CASE("WebSocket endpoint components use a validated wss URL")
{
    boost::asio::io_context io;
    nom::web_socket client(io.get_executor(), "localhost", 9443, "/feed%2Fone?topic=two");
    CHECK(client.url().scheme() == "wss");
    CHECK(client.url().host() == "localhost");
    CHECK(client.url().port() == 9443);
    CHECK(client.url().target() == "/feed%2Fone?topic=two");

    nom::web_socket ipv6_client(io.get_executor(), "[::1]", 443, "");
    CHECK(ipv6_client.url().host() == "::1");
    CHECK(ipv6_client.url().target() == "/");
    CHECK_THROWS_AS(nom::web_socket(io.get_executor(), "localhost", 0, "/"), std::invalid_argument);
    CHECK_THROWS_AS(nom::web_socket(io.get_executor(), "localhost", 443, "/feed#fragment"), std::invalid_argument);
}

TEST_CASE("An unused WebSocket retains its initial session through repeated close")
{
    nom::init_logger(nom::main_logger);
    boost::asio::io_context io;
    nom::web_socket client(io.get_executor(), nom::url("wss://localhost/"));
    const auto initial_id = client.session_id();
    CHECK(initial_id != "WS[NONE]");
    CHECK_FALSE(client.send("hello"));
    client.close();
    client.close();
    CHECK(client.status() == nom::ws_status::init);
    CHECK(client.session_id() == initial_id);
    CHECK_FALSE(client.is_connected());
    CHECK(io.poll() == 0);
}

TEST_CASE("Closing or destroying while resolving suppresses queued events")
{
    nom::init_logger(nom::main_logger);
    boost::asio::io_context io;
    unsigned calls = 0;
    const nom::web_socket::delegate callbacks{[&](nom::web_socket&, const auto&) { ++calls; }};
    {
        nom::web_socket client(io.get_executor(), nom::url("wss://127.0.0.1:1/"));
        const auto initial_id = client.session_id();
        client.close();
        client.connect(callbacks);
        CHECK(client.session_id() == initial_id);
        CHECK(client.status() == nom::ws_status::connecting);
        CHECK_FALSE(client.send("hello"));
        CHECK(client.status() == nom::ws_status::connecting);
        const auto first_id = client.session_id();
        client.connect(callbacks); // Cancels the first session before creating a replacement.
        CHECK(client.session_id() != first_id);
        const auto second_id = client.session_id();
        client.close();
        CHECK(client.status() == nom::ws_status::conn_failed);
        CHECK(client.session_id() == second_id);
        client.close();
        CHECK(client.status() == nom::ws_status::conn_failed);
        CHECK(client.session_id() == second_id);
        client.connect(callbacks); // Destruction cancels the third session.
        CHECK(client.session_id() != second_id);
    }
    io.run();
    CHECK(calls == 0);
}

TEST_CASE("Queued session handlers can be destroyed without running after the owner is gone")
{
    nom::init_logger(nom::main_logger);
    unsigned calls = 0;
    {
        boost::asio::io_context io;
        {
            nom::web_socket client(io.get_executor(), nom::url("wss://127.0.0.1:1/"));
            const nom::web_socket::delegate callbacks{[&](nom::web_socket&, const auto&) { ++calls; }};
            client.connect(callbacks);
            client.connect(callbacks);
        }
        // Destroying io releases the canceled handlers and their sessions without invoking them.
    }
    CHECK(calls == 0);
}
