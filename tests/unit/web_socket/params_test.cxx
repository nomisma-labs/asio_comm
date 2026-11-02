// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

#include <catch2/catch_test_macros.hpp>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/io_context.hpp>

import std;
import nom.asio_comm;
import nom.spd_logger;

TEST_CASE("WebSocket parameter validation checks numeric bounds")
{
    nom::ws_params params;
    REQUIRE(params.validate().has_value());
    CHECK(params.max_message_size == 16 * 1024 * 1024);
    params.max_message_size = 0;
    REQUIRE_FALSE(params.validate());
    CHECK(params.validate().error() == "/max_message_size must be positive");
    params.max_message_size = 1;
    CHECK(params.validate().has_value());

    params.async_operation_timeout_sec = 0;
    REQUIRE_FALSE(params.validate());
    CHECK(params.validate().error() == "/async_operation_timeout_sec must be positive");
    params.async_operation_timeout_sec = 1;

    CHECK_FALSE(params.receive_watchdog);
    params.receive_watchdog = nom::ws_params::receive_data_watchdog{};
    CHECK(params.receive_watchdog->timeout_sec == 20);
    REQUIRE(params.validate().has_value());
    params.receive_watchdog->timeout_sec = 0;
    REQUIRE_FALSE(params.validate());
    CHECK(params.validate().error() == "/receive_watchdog/timeout_sec must be positive");
    params.receive_watchdog->timeout_sec = 1;
    CHECK(params.validate().has_value());
    params.receive_watchdog.reset();
    CHECK(params.validate().has_value());

    const auto max_buffer = static_cast<unsigned>(std::numeric_limits<int>::max());
    for (const unsigned invalid : {0u, max_buffer + 1u, std::numeric_limits<unsigned>::max()})
    {
        CAPTURE(invalid);
        params.socket_send_buf_size = invalid;
        REQUIRE_FALSE(params.validate());
        CHECK(params.validate().error().starts_with("/socket_send_buf_size"));
        params.socket_send_buf_size = max_buffer;
        params.socket_recv_buf_size = invalid;
        REQUIRE_FALSE(params.validate());
        CHECK(params.validate().error().starts_with("/socket_recv_buf_size"));
        params.socket_recv_buf_size = max_buffer;
        CHECK(params.validate().has_value());
    }
    // Setup timeout and receive watchdog timeout have no required ordering.
    params.receive_watchdog = nom::ws_params::receive_data_watchdog{.timeout_sec = 60};
    CHECK(params.validate().has_value());
}

TEST_CASE("Ping policies validate independently of receive watchdog monitoring")
{
    nom::ws_params params;
    CHECK_FALSE(params.passive_ping);
    CHECK_FALSE(params.active_ping);
    for (const bool receive_watchdog_enabled : {false, true})
    {
        if (receive_watchdog_enabled)
            params.receive_watchdog = nom::ws_params::receive_data_watchdog{.timeout_sec = 1};
        else
            params.receive_watchdog.reset();
        params.passive_ping = nom::ws_params::server_ping_watchdog{.ping_timeout_sec = 0};
        REQUIRE_FALSE(params.validate());
        CHECK(params.validate().error() == "/passive_ping/ping_timeout_sec must be positive");
        params.passive_ping->ping_timeout_sec = 60;
        REQUIRE(params.validate().has_value());

        params.active_ping = nom::ws_params::client_ping_probe{.ping_period_sec = 0, .pong_timeout_sec = 1};
        REQUIRE_FALSE(params.validate());
        CHECK(params.validate().error() == "/active_ping/ping_period_sec must be positive");
        params.active_ping->ping_period_sec = 2;
        params.active_ping->pong_timeout_sec = 0;
        REQUIRE_FALSE(params.validate());
        CHECK(params.validate().error() == "/active_ping/pong_timeout_sec must be positive");
        params.active_ping->pong_timeout_sec = 5;
        CHECK(params.validate().has_value());
        params.passive_ping.reset();
        CHECK(params.validate().has_value());
        params.active_ping.reset();
        CHECK(params.validate().has_value());
    }
}

TEST_CASE("WebSocket parameters validate HTTP syntax and header ownership")
{
    nom::ws_params params;
    for (const auto& invalid : {std::string{}, std::string{" \t"}, std::string{"agent\r\nother"}})
    {
        params.agent_name = invalid;
        REQUIRE_FALSE(params.validate());
        CHECK(params.validate().error().starts_with("/agent_name"));
    }
    params.agent_name = nom::ws_params::default_agent_name;

    for (const auto& invalid : {std::string{},
                                std::string{"Bad Name"},
                                std::string{"Bad:Name"},
                                std::string{"X\0Y", 3},
                                std::string{"X-\x80"}})
    {
        params.headers = {{.name = invalid, .value = "value"}};
        REQUIRE_FALSE(params.validate());
        CHECK(params.validate().error().starts_with("/headers/0/name"));
    }
    for (const char invalid : {'\0', '\r', '\n', '\x01', '\x1f', '\x7f'})
    {
        params.headers = {{.name = "X-Test", .value = std::string{"before"} + invalid + "after"}};
        REQUIRE_FALSE(params.validate());
        CHECK(params.validate().error().starts_with("/headers/0/value"));
        params.headers.clear();
        params.agent_name = std::string{"agent"} + invalid;
        REQUIRE_FALSE(params.validate());
        CHECK(params.validate().error().starts_with("/agent_name"));
        params.agent_name = nom::ws_params::default_agent_name;
    }

    for (const auto reserved : {"hOsT",
                                "Connection",
                                "Upgrade",
                                "Sec-WebSocket-Key",
                                "Sec-WebSocket-Version",
                                "Sec-WebSocket-Accept",
                                "Sec-WebSocket-Extensions",
                                "Content-Length",
                                "Transfer-Encoding"})
    {
        CAPTURE(reserved);
        params.headers = {{.name = reserved, .value = "value"}};
        REQUIRE_FALSE(params.validate());
        CHECK(params.validate().error() == "/headers/0/name is reserved for the WebSocket handshake");
    }
    params.headers = {{.name = "X-Feed", .value = "first"}, {.name = "x-FEED", .value = "second"}};
    REQUIRE_FALSE(params.validate());
    CHECK(params.validate().error() == "/headers/1/name duplicates an earlier header");

    params.headers = {{.name = "User-Agent", .value = "explicit-agent/1"},
                      {.name = "Authorization", .value = "Bearer token"},
                      {.name = "Sec-WebSocket-Protocol", .value = "market-data"},
                      {.name = "!#$%&'*+-.^_`|~", .value = ""},
                      {.name = "X-Opaque", .value = "one\ttwo\x80\xff"}};
    CHECK(params.validate().has_value());
}

TEST_CASE("WebSocket parameters reject HTTP fields beyond the backend limit")
{
    constexpr std::size_t max_field_size = 65533;
    nom::ws_params params;
    params.agent_name.assign(max_field_size, 'a');
    REQUIRE(params.validate().has_value());
    params.agent_name.push_back('a');
    REQUIRE_FALSE(params.validate());
    CHECK(params.validate().error().starts_with("/agent_name"));
    params.agent_name = nom::ws_params::default_agent_name;

    params.headers = {{.name = std::string(max_field_size, 'X'), .value = "value"}};
    REQUIRE(params.validate().has_value());
    params.headers.front().name.push_back('X');
    REQUIRE_FALSE(params.validate());
    CHECK(params.validate().error().starts_with("/headers/0/name"));
    params.headers = {{.name = "X-Test", .value = std::string(max_field_size, 'x')}};
    REQUIRE(params.validate().has_value());
    params.headers.front().value.push_back('x');
    REQUIRE_FALSE(params.validate());
    CHECK(params.validate().error().starts_with("/headers/0/value"));
}

TEST_CASE("Parameter updates are transactional and require an inactive session")
{
    STATIC_REQUIRE((std::same_as<decltype(std::declval<nom::web_socket&>().params()), const nom::ws_params&>));
    STATIC_REQUIRE(
        (std::same_as<decltype(std::declval<const nom::ws_params&>().validate()), std::expected<void, std::string>>));
    nom::init_logger(nom::main_logger);
    boost::asio::io_context io;
    nom::web_socket client(io.get_executor(), nom::url("wss://127.0.0.1:1/"));
    const auto id = client.session_id();
    auto params = client.params();
    params.agent_name = "configured-agent/1";
    params.headers = {{.name = "X-Feed", .value = "original"}};
    params.socket_send_buf_size = 4096;
    params.max_message_size = 32 * 1024 * 1024;
    params.receive_watchdog = nom::ws_params::receive_data_watchdog{.timeout_sec = 7};
    params.passive_ping = nom::ws_params::server_ping_watchdog{.ping_timeout_sec = 60};
    params.active_ping = nom::ws_params::client_ping_probe{.ping_period_sec = 5, .pong_timeout_sec = 10};
    client.update_params(params);
    client.update_params(client.params());
    CHECK(client.status() == nom::ws_status::init);
    CHECK(client.session_id() == id);
    CHECK(client.params().agent_name == params.agent_name);
    CHECK(client.params().headers.front().value == "original");
    CHECK(client.params().socket_send_buf_size == 4096);
    CHECK(client.params().max_message_size == params.max_message_size);
    REQUIRE(client.params().receive_watchdog);
    CHECK(client.params().receive_watchdog->timeout_sec == 7);
    REQUIRE(client.params().passive_ping);
    REQUIRE(client.params().active_ping);
    CHECK(client.params().passive_ping->ping_timeout_sec == 60);
    CHECK(client.params().active_ping->ping_period_sec == 5);
    CHECK(client.params().active_ping->pong_timeout_sec == 10);

    auto invalid = params;
    invalid.receive_watchdog->timeout_sec = 0;
    invalid.headers.front().value = "must-not-commit";
    const auto error = invalid.validate().error();
    try
    {
        client.update_params(invalid);
        FAIL("Expected invalid_argument for invalid parameters");
    }
    catch (const std::invalid_argument& exception)
    {
        CHECK(exception.what() == error);
    }
    CHECK(client.params().headers.front().value == "original");
    REQUIRE(client.params().receive_watchdog);
    CHECK(client.params().receive_watchdog->timeout_sec == params.receive_watchdog->timeout_sec);

    unsigned calls = 0;
    const nom::web_socket::delegate callbacks{[&](nom::web_socket&, const auto&) { ++calls; }};
    client.connect(callbacks);
    REQUIRE(client.status() == nom::ws_status::connecting);
    params.agent_name = "next-agent/1";
    CHECK_THROWS_AS(client.update_params(params), std::logic_error);
    CHECK_THROWS_AS(client.update_params(invalid), std::invalid_argument);
    CHECK(client.params().agent_name == "configured-agent/1");
    CHECK(client.status() == nom::ws_status::connecting);
    CHECK(client.session_id() == id);
    client.close();
    client.update_params(params);
    CHECK(client.status() == nom::ws_status::conn_failed);
    CHECK(client.session_id() == id);
    CHECK(client.params().agent_name == "next-agent/1");
    client.connect(callbacks);
    client.close();
    io.run();
    CHECK(calls == 0);
}
