// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0

#include <catch2/catch_test_macros.hpp>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/io_context.hpp>

import std;
import nom.asio_comm;
import nom.spd_logger;

namespace
{
    struct injected_allocation_failure : std::bad_alloc
    {
    };

    // Only fail on the calling thread when the client reaches the selected state.
    thread_local const nom::web_socket* failing_client = nullptr;
    thread_local nom::ws_status failing_status = nom::ws_status::connecting;
    thread_local bool failure_injected = false;
    thread_local unsigned allocations_before_failure = 0;

    struct connection_allocation_failure
    {
        explicit connection_allocation_failure(const nom::web_socket& client,
                                               nom::ws_status status = nom::ws_status::connecting)
        {
            failing_client = &client;
            failing_status = status;
            failure_injected = false;
            allocations_before_failure = 0;
        }

        ~connection_allocation_failure()
        {
            failing_client = nullptr;
        }
    };
} // namespace

// Test-executable replacement: production code needs no allocation-failure hook.
// Keep allocation boundaries visible: GCC diagnoses an inlined free() as mismatched with new.
[[gnu::noinline]] void* operator new(std::size_t size)
{
    if (failing_client && failing_status == failing_client->status())
    {
        if (allocations_before_failure == 0)
        {
            failing_client = nullptr;
            failure_injected = true;
            throw injected_allocation_failure{};
        }
        --allocations_before_failure;
    }
    if (void* memory = std::malloc(size ? size : 1))
        return memory;
    throw std::bad_alloc{};
}

// Catch2 also uses nothrow new; keep it paired with the malloc/free replacement under ASan.
[[gnu::noinline]] void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    try
    {
        return ::operator new(size);
    }
    catch (...)
    {
        return nullptr;
    }
}

[[gnu::noinline]] void operator delete(void* memory) noexcept
{
    std::free(memory);
}

[[gnu::noinline]] void operator delete(void* memory, std::size_t) noexcept
{
    std::free(memory);
}

[[gnu::noinline]] void operator delete(void* memory, const std::nothrow_t&) noexcept
{
    std::free(memory);
}

TEST_CASE("Connection initiation exceptions leave a failed session and preserve the exception")
{
    nom::init_logger(nom::main_logger);
    boost::asio::io_context io;
    nom::web_socket client(io.get_executor(), nom::url("wss://127.0.0.1:1/"));
    unsigned calls = 0;
    const nom::web_socket::ws_delegate callbacks{[&](nom::web_socket&, const auto&) { ++calls; }};

    bool caught_original = false;
    {
        connection_allocation_failure failure(client);
        try
        {
            client.connect(callbacks);
        }
        catch (const injected_allocation_failure&)
        {
            caught_original = true;
        }
    }
    REQUIRE(caught_original);
    CHECK(nom::ws_status::conn_failed == client.status());
    CHECK(calls == 0);
    io.poll();
    CHECK(calls == 0);
    CHECK(nom::ws_status::conn_failed == client.status());
    CHECK_FALSE(client.send("after connection failure"));
    CHECK(nom::ws_status::conn_failed == client.status());
    const auto failed_id = client.session_id();
    client.close();
    CHECK(nom::ws_status::conn_failed == client.status());
    CHECK(client.session_id() == failed_id);

    // A new attempt replaces the failed session and can still be canceled normally.
    io.restart();
    client.connect(callbacks);
    CHECK(nom::ws_status::connecting == client.status());
    client.close();
    io.run();
    CHECK(nom::ws_status::conn_failed == client.status());
    CHECK(calls == 0);
}

TEST_CASE("Closing pending setup does not allocate and suppresses queued events")
{
    STATIC_REQUIRE(noexcept(std::declval<nom::web_socket&>().close()));

    nom::init_logger(nom::main_logger);
    boost::asio::io_context io;
    nom::web_socket client(io.get_executor(), nom::url("wss://127.0.0.1:1/"));
    unsigned calls = 0;
    client.connect(nom::web_socket::ws_delegate{[&](nom::web_socket&, const auto&) { ++calls; }});
    REQUIRE(nom::ws_status::connecting == client.status());

    {
        // Releasing setup cancels resolution without allocating a replacement cancellation token.
        connection_allocation_failure failure(client, nom::ws_status::conn_failed);
        client.close();
    }
    CHECK_FALSE(failure_injected);
    CHECK(nom::ws_status::conn_failed == client.status());
    CHECK(calls == 0);

    // The pending resolver completion must not reach the delegate after setup is released.
    io.run();
    CHECK(calls == 0);
    client.close();
    CHECK(nom::ws_status::conn_failed == client.status());
}

TEST_CASE("Move construction allocation failure preserves the source attempt")
{
    nom::init_logger(nom::main_logger);
    boost::asio::io_context io;
    nom::web_socket source(io.get_executor(), nom::url("wss://127.0.0.1:1/"));
    unsigned calls = 0;
    source.connect(nom::web_socket::ws_delegate{[&](nom::web_socket&, const auto&) { ++calls; }});
    const auto original_id = source.session_id();
    bool caught_original = false;
    {
        connection_allocation_failure failure(source);
        try
        {
            nom::web_socket destination(std::move(source));
        }
        catch (const injected_allocation_failure&)
        {
            caught_original = true;
        }
    }
    REQUIRE(caught_original);
    CHECK(failure_injected);
    CHECK(source.status() == nom::ws_status::connecting);
    CHECK(source.session_id() == original_id);
    source.close();
    io.run();
    CHECK(calls == 0);
}

TEST_CASE("Parameter copy failure preserves all installed settings")
{
    nom::init_logger(nom::main_logger);
    boost::asio::io_context io;
    nom::web_socket client(io.get_executor(), nom::url("wss://127.0.0.1:1/"));
    auto original = client.params();
    original.headers = {{.name = "X-Feed", .value = "original"}};
    original.agent_name = "original-agent";
    client.update_params(original);
    const auto id = client.session_id();
    auto candidate = original;
    candidate.headers.front().value.assign(128, 'x');
    candidate.agent_name.assign(128, 'a');
    candidate.active_ping = nom::ws_params::client_ping_probe{.ping_period_sec = 5, .pong_timeout_sec = 10};
    for (const unsigned permitted_allocations : {0u, 1u, 2u})
    {
        CAPTURE(permitted_allocations);
        bool caught_original = false;
        {
            connection_allocation_failure failure(client, nom::ws_status::init);
            allocations_before_failure = permitted_allocations;
            try
            {
                client.update_params(candidate);
            }
            catch (const injected_allocation_failure&)
            {
                caught_original = true;
            }
        }
        REQUIRE(failure_injected);
        REQUIRE(caught_original);
        CHECK(client.params().headers.front().value == "original");
        CHECK(client.params().agent_name == "original-agent");
        CHECK_FALSE(client.params().active_ping);
        CHECK(client.session_id() == id);
        CHECK(client.status() == nom::ws_status::init);
        CHECK(io.poll() == 0);
    }
}
