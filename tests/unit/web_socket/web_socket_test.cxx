// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

#include <catch2/catch_test_macros.hpp>

import std;
import nom.asio_comm;
import nom.spd_logger;

TEST_CASE("WebSocket uses the base URL and enum utilities")
{
    nom::asio::io_context io;
    nom::url address("wss", "localhost", 9443, "/feed%2Fone?topic=two");
    nom::web_socket client(io.get_executor(), address);
    address = nom::url("wss://other.example/"); // The client owns its endpoint.
    CHECK(client.url().scheme() == "wss");
    CHECK(client.url().host() == "localhost");
    CHECK(client.url().port() == 9443);
    CHECK(client.url().target() == "/feed%2Fone?topic=two");
    CHECK(client.status() == nom::ws_status::disconnected);
    CHECK(nom::enum_to_string(client.status()) == "disconnected");
    CHECK(nom::enum_to_string(static_cast<nom::ws_status>(255)) == "ws_status::<unknown>");
    CHECK_THROWS_AS(nom::web_socket(io.get_executor(), nom::url("https://localhost/")), std::invalid_argument);
    STATIC_REQUIRE_FALSE(std::is_move_constructible_v<nom::web_socket>);
    STATIC_REQUIRE_FALSE(std::is_copy_constructible_v<nom::web_socket>);
}

TEST_CASE("WebSocket endpoint components use a validated wss URL")
{
    nom::asio::io_context io;
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

TEST_CASE("Disconnected WebSocket rejects sends and tolerates repeated close")
{
    nom::init_logger(nom::main_logger);
    nom::asio::io_context io;
    nom::web_socket client(io.get_executor(), nom::url("wss://localhost/"));
    CHECK_FALSE(client.send("hello"));
    client.close();
    client.close();
    CHECK_FALSE(client.is_connected());
}

TEST_CASE("Closing or destroying while resolving suppresses queued events")
{
    nom::init_logger(nom::main_logger);
    nom::asio::io_context io;
    unsigned calls = 0;
    const nom::web_socket::delegate callbacks{[&](nom::web_socket&, const auto&) { ++calls; }};
    {
        nom::web_socket client(io.get_executor(), nom::url("wss://127.0.0.1:1/"));
        client.connect(callbacks);
        CHECK(client.status() == nom::ws_status::connecting);
        CHECK_FALSE(client.send("hello"));
        CHECK(client.status() == nom::ws_status::connecting);
        const auto first_id = client.session_id();
        client.connect(callbacks); // Cancels the first session before creating a replacement.
        CHECK(client.session_id() != first_id);
        client.close();
        CHECK(client.status() == nom::ws_status::disconnected);
        client.connect(callbacks); // Destruction cancels the third session.
    }
    io.run();
    CHECK(calls == 0);
}
