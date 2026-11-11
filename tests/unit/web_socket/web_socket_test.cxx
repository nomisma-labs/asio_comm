// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0

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
    STATIC_REQUIRE(std::is_move_constructible_v<nom::web_socket>);
    STATIC_REQUIRE(std::is_nothrow_move_assignable_v<nom::web_socket>);
    STATIC_REQUIRE_FALSE(std::is_copy_constructible_v<nom::web_socket>);
    STATIC_REQUIRE_FALSE(std::is_copy_assignable_v<nom::web_socket>);
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
    const nom::web_socket::ws_delegate callbacks{[&](nom::web_socket&, const auto&) { ++calls; }};
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
            const nom::web_socket::ws_delegate callbacks{[&](nom::web_socket&, const auto&) { ++calls; }};
            client.connect(callbacks);
            client.connect(callbacks);
        }
        // Destroying io releases the canceled handlers and their sessions without invoking them.
    }
    CHECK(calls == 0);
}

TEST_CASE("Pending sessions retain callbacks until canceled operations are released")
{
    nom::init_logger(nom::main_logger);
    std::weak_ptr<unsigned> callback_lifetime;
    unsigned calls = 0;
    {
        boost::asio::io_context io;
        {
            nom::web_socket client(io.get_executor(), nom::url("wss://127.0.0.1:1/"));
            auto lifetime = std::make_shared<unsigned>(0);
            callback_lifetime = lifetime;
            client.connect(nom::web_socket::ws_delegate{[lifetime, &calls](nom::web_socket&, const auto&)
                                                        {
                                                            ++*lifetime;
                                                            ++calls;
                                                        }});
        }
        CHECK_FALSE(callback_lifetime.expired());

        SECTION("Canceled completions are drained")
        {
            io.run();
            CHECK(callback_lifetime.expired());
        }
        SECTION("Canceled completions are discarded with their context")
        {
        }
    }
    CHECK(callback_lifetime.expired());
    CHECK(calls == 0);
}

TEST_CASE("Move construction transfers an unused session and leaves a usable source")
{
    nom::init_logger(nom::main_logger);
    boost::asio::io_context io;
    nom::web_socket source(io.get_executor(), nom::url("wss://127.0.0.1:1/source"));
    auto params = source.params();
    params.agent_name = "move-test";
    params.headers = {{.name = "X-Move", .value = "preserved"}};
    source.update_params(params);
    const auto original_id = source.session_id();

    nom::web_socket destination(std::move(source));
    CHECK(destination.session_id() == original_id);
    CHECK(destination.status() == nom::ws_status::init);
    CHECK(destination.url().target() == "/source");
    CHECK(destination.params().agent_name == "move-test");
    REQUIRE(destination.params().headers.size() == 1);
    CHECK(destination.params().headers.front().value == "preserved");
    CHECK(source.status() == nom::ws_status::init);
    CHECK(source.session_id() != original_id);

    unsigned calls = 0;
    const nom::web_socket::ws_delegate callbacks{[&](nom::web_socket&, const auto&) { ++calls; }};
    destination.connect(callbacks);
    source.connect(callbacks);
    destination.close();
    source.close();
    io.run();
    CHECK(calls == 0);
}

TEST_CASE("Move assignment cancels the destination attempt and preserves both holders")
{
    nom::init_logger(nom::main_logger);
    boost::asio::io_context io;
    boost::asio::io_context destination_io;
    unsigned calls = 0;
    const nom::web_socket::ws_delegate callbacks{[&](nom::web_socket&, const auto&) { ++calls; }};
    nom::web_socket source(io.get_executor(), nom::url("wss://127.0.0.1:1/source"));
    nom::web_socket destination(destination_io.get_executor(), nom::url("wss://127.0.0.1:1/destination"));
    source.connect(callbacks);
    destination.connect(callbacks);
    const auto source_id = source.session_id();
    const auto replaced_id = destination.session_id();

    SECTION("The destination still has a pending attempt")
    {
        CHECK(destination.status() == nom::ws_status::connecting);
    }
    SECTION("The destination is already terminal")
    {
        destination.close();
        CHECK(destination.status() == nom::ws_status::conn_failed);
    }

    destination = std::move(source);
    CHECK(destination.session_id() == source_id);
    CHECK(destination.status() == nom::ws_status::connecting);
    CHECK(destination.url().target() == "/source");
    CHECK(source.session_id() == replaced_id);
    CHECK(source.status() == nom::ws_status::conn_failed);
    CHECK(source.url().target() == "/destination");

    auto& same_destination = destination;
    destination = std::move(same_destination);
    CHECK(destination.session_id() == source_id);
    CHECK(destination.status() == nom::ws_status::connecting);
    destination.close();
    source.connect(callbacks);
    source.close();
    io.run();
    destination_io.run();
    CHECK(calls == 0);
}

TEST_CASE("Moving terminal sessions preserves failure status and usable owner links")
{
    nom::init_logger(nom::main_logger);
    boost::asio::io_context io;
    unsigned calls = 0;
    const nom::web_socket::ws_delegate callbacks{[&](nom::web_socket&, const auto&) { ++calls; }};
    nom::web_socket source(io.get_executor(), nom::url("wss://127.0.0.1:1/"));
    source.connect(callbacks);
    source.close();
    const auto failed_id = source.session_id();

    nom::web_socket destination(std::move(source));
    CHECK(destination.status() == nom::ws_status::conn_failed);
    CHECK(destination.session_id() == failed_id);
    source.connect(callbacks); // Exercise the replacement's owner link before another move can repair it.
    CHECK(source.status() == nom::ws_status::connecting);
    source = std::move(destination); // Replace the pending attempt with the terminal session.
    CHECK(source.status() == nom::ws_status::conn_failed);
    CHECK(source.session_id() == failed_id);
    CHECK(destination.status() == nom::ws_status::conn_failed);
    source.close();
    source.connect(callbacks);
    destination.connect(callbacks);
    CHECK(source.status() == nom::ws_status::connecting);
    CHECK(destination.status() == nom::ws_status::connecting);
    source.close();
    destination.close();
    io.run();
    CHECK(calls == 0);
}
