// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/io_context.hpp>
#include <catch2/catch_test_macros.hpp>

import std;
import nom.asio_comm;

TEST_CASE("HTTP request validation rejects injection and conflicting framing")
{
    nom::http_request request{.method = "POST",
                              .target = "/orders?symbol=BTC%2FUSD",
                              .headers = {{"Content-Type", "application/json"}},
                              .body = "{}"};
    CHECK(request.validate());
    for (const auto name :
         {"Host", "CONTENT-LENGTH", "Transfer-Encoding", "connection", "Expect", "Upgrade", "tRaIlEr"})
    {
        request.headers = {{name, "value"}};
        CHECK_FALSE(request.validate());
    }
    request.headers = {{"X-Test", "a\r\nb"}};
    CHECK_FALSE(request.validate());
    request.headers = {{"Bad Name", "value"}};
    CHECK_FALSE(request.validate());
    request.headers.clear();
    for (const auto target : {"", "https://other.example/", "/a b", "/a\r\nb", "/a#fragment"})
    {
        request.target = target;
        CHECK_FALSE(request.validate());
    }
    request.target = "/";
    request.method = "CONNECT";
    CHECK_FALSE(request.validate());
    request.method = "HEAD";
    CHECK(request.validate());
}

TEST_CASE("HTTPS targets require encoded HTTP origin-form syntax")
{
    nom::http_request request;
    for (const auto* target : {"/", "/orders?symbol=BTC%2FUSD", "/a%20b?q=%23", "/?", "//path"})
    {
        CAPTURE(target);
        request.target = target;
        CHECK(request.validate());
    }
    for (const auto& target : {std::string{"/%"},
                               std::string{"/%0"},
                               std::string{"/%GG"},
                               std::string{"/orders?q=%ZZ"},
                               std::string{"/caf\xc3\xa9"},
                               std::string{"/a\0b", 4}})
    {
        CAPTURE(target);
        request.target = target;
        const auto result = request.validate();
        REQUIRE_FALSE(result);
        CHECK(result.error().starts_with("/target"));
        CHECK(result.error().contains("Boost.URL"));
    }
}

TEST_CASE("HTTPS header diagnostics identify the field without exposing its value")
{
    nom::http_request request;
    request.headers = {{"Authorization", "Bearer private-token"}, {"X-Test", "private-value"}};
    for (const auto& name : {std::string{},
                             std::string{"Bad Name"},
                             std::string{"Bad:Name"},
                             std::string{"X\0Y", 3},
                             std::string{"X-\x80"}})
    {
        request.headers[1].name = name;
        const auto result = request.validate();
        REQUIRE_FALSE(result);
        CHECK(result.error().starts_with("/headers/1/name"));
        CHECK_FALSE(result.error().contains("private-token"));
        CHECK_FALSE(result.error().contains("private-value"));
    }
    request.headers[1].name = "X-Test";
    for (const char control : {'\0', '\r', '\n', '\x01', '\x1f', '\x7f'})
    {
        request.headers[1].value = std::string{"private-value"} + control;
        const auto result = request.validate();
        REQUIRE_FALSE(result);
        CHECK(result.error().starts_with("/headers/1/value"));
        CHECK(result.error().contains("control character"));
        CHECK_FALSE(result.error().contains("private-value"));
    }
    request.headers[1] = {"!#$%&'*+-.^_`|~", "one\ttwo\x80\xff"};
    CHECK(request.validate());
    request.headers[1].value.clear();
    CHECK(request.validate());
}

TEST_CASE("HTTPS configuration is validated and cannot change during a connection attempt")
{
    boost::asio::io_context io;
    nom::https client(io.get_executor(), nom::url("https://localhost:1/path?q=x"));
    CHECK(client.status() == nom::https_status::init);
    CHECK(client.url().target() == "/path?q=x");
    CHECK(client.params().validate());
    auto params = client.params();
    params.request_timeout_sec = 0;
    CHECK_FALSE(params.validate());
    CHECK_THROWS_AS(client.update_params(params), std::invalid_argument);
    CHECK(client.params().request_timeout_sec == 30);
    params = client.params();
    params.max_pending_requests = 0;
    CHECK_FALSE(params.validate());
    CHECK_THROWS_AS(nom::https(io.get_executor(), nom::url("wss://localhost/")), std::invalid_argument);
    CHECK_THROWS_AS(client.send(nom::http_request{}), std::logic_error);
    unsigned calls{};
    const nom::https::https_delegate handlers{[&](auto&&...) { ++calls; }};
    client.connect(handlers);
    CHECK(client.status() == nom::https_status::connecting);
    CHECK_THROWS_AS(client.update_params(client.params()), std::logic_error);
    client.close();
    client.close();
    CHECK(client.status() == nom::https_status::conn_failed);
    io.run();
    CHECK(calls == 0);
}

TEST_CASE("HTTPS replacement and destruction while resolving suppress callbacks")
{
    boost::asio::io_context io;
    unsigned calls{};
    const nom::https::https_delegate handlers{[&](auto&&...) { ++calls; }};
    {
        nom::https client(io.get_executor(), nom::url("https://localhost:1/"));
        client.connect(handlers);
        const auto first = client.session_id();
        client.connect(handlers);
        CHECK(client.session_id() != first);
    }
    io.run();
    CHECK(calls == 0);
}

TEST_CASE("HTTPS move construction preserves identity and leaves a reusable source")
{
    STATIC_REQUIRE(std::is_move_constructible_v<nom::https>);
    STATIC_REQUIRE(std::is_nothrow_move_assignable_v<nom::https>);
    STATIC_REQUIRE_FALSE(std::is_copy_constructible_v<nom::https>);
    STATIC_REQUIRE_FALSE(std::is_copy_assignable_v<nom::https>);

    boost::asio::io_context io;
    nom::https source(io.get_executor(), nom::url("https://localhost:1/source"));
    auto params = source.params();
    params.request_timeout_sec = 17;
    source.update_params(params);
    const auto id = source.session_id();

    nom::https destination(std::move(source));
    CHECK(destination.session_id() == id);
    CHECK(destination.status() == nom::https_status::init);
    CHECK(destination.url().target() == "/source");
    CHECK(destination.params().request_timeout_sec == 17);
    CHECK(source.session_id() != id);
    CHECK(source.status() == nom::https_status::init);
    CHECK(source.url().target() == "/source");
    CHECK(source.params().request_timeout_sec == nom::https_params{}.request_timeout_sec);

    auto& same_destination = destination;
    destination = std::move(same_destination);
    CHECK(destination.session_id() == id);
    CHECK(destination.status() == nom::https_status::init);

    unsigned calls{};
    source.connect(nom::https::https_delegate{[&](auto&&...) { ++calls; }});
    CHECK(source.status() == nom::https_status::connecting);
    source.close();
    io.run();
    CHECK(calls == 0);
    CHECK(destination.status() == nom::https_status::init);
}

TEST_CASE("HTTPS move assignment swaps configuration and cancels only the replaced attempt")
{
    boost::asio::io_context source_io, destination_io;
    unsigned calls{};
    const nom::https::https_delegate callbacks{[&](auto&&...) { ++calls; }};
    nom::https source(source_io.get_executor(), nom::url("https://localhost:1/source"));
    nom::https destination(destination_io.get_executor(), nom::url("https://localhost:2/destination"));
    auto params = source.params();
    params.request_timeout_sec = 17;
    source.update_params(params);
    params.request_timeout_sec = 23;
    destination.update_params(params);
    source.connect(callbacks);
    destination.connect(callbacks);
    const auto source_id = source.session_id();
    const auto replaced_id = destination.session_id();

    destination = std::move(source);
    CHECK(destination.session_id() == source_id);
    CHECK(destination.status() == nom::https_status::connecting);
    CHECK(destination.url().target() == "/source");
    CHECK(destination.params().request_timeout_sec == 17);
    CHECK(source.session_id() == replaced_id);
    CHECK(source.status() == nom::https_status::conn_failed);
    CHECK(source.url().target() == "/destination");
    CHECK(source.params().request_timeout_sec == 23);

    auto& same_destination = destination;
    destination = std::move(same_destination);
    CHECK(destination.status() == nom::https_status::connecting);
    destination.close();
    source.connect(callbacks);
    source.close();
    source_io.run();
    destination_io.run();
    CHECK(calls == 0);
}

TEST_CASE("HTTPS moves preserve terminal sessions and their sources remain reusable")
{
    boost::asio::io_context io;
    unsigned calls{};
    const nom::https::https_delegate callbacks{[&](auto&&...) { ++calls; }};
    nom::https source(io.get_executor(), nom::url("https://localhost:1/"));
    source.connect(callbacks);
    source.close();
    const auto id = source.session_id();
    nom::https destination(std::move(source));
    CHECK(destination.session_id() == id);
    CHECK(destination.status() == nom::https_status::conn_failed);
    source.connect(callbacks);
    source = std::move(destination);
    CHECK(source.session_id() == id);
    CHECK(source.status() == nom::https_status::conn_failed);
    CHECK(destination.status() == nom::https_status::conn_failed);
    destination.connect(callbacks);
    destination.close();
    io.run();
    CHECK(calls == 0);
}
