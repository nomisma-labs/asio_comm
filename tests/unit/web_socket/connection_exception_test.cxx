// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

#include <catch2/catch_test_macros.hpp>

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

    struct connection_allocation_failure
    {
        explicit connection_allocation_failure(const nom::web_socket& client,
                                               nom::ws_status status = nom::ws_status::connecting)
        {
            failing_client = &client;
            failing_status = status;
            failure_injected = false;
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
        failing_client = nullptr;
        failure_injected = true;
        throw injected_allocation_failure{};
    }
    if (void* memory = std::malloc(size ? size : 1))
        return memory;
    throw std::bad_alloc{};
}

[[gnu::noinline]] void operator delete(void* memory) noexcept
{
    std::free(memory);
}

[[gnu::noinline]] void operator delete(void* memory, std::size_t) noexcept
{
    std::free(memory);
}

TEST_CASE("Connection initiation exceptions leave a failed session and preserve the exception")
{
    nom::init_logger(nom::main_logger);
    nom::asio::io_context io;
    nom::web_socket client(io.get_executor(), nom::url("wss://127.0.0.1:1/"));
    unsigned calls = 0;
    const nom::web_socket::delegate callbacks{[&](nom::web_socket&, const auto&) { ++calls; }};

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
    CHECK(nom::ws_status::failed == client.status());
    CHECK(calls == 0);
    io.poll();
    CHECK(calls == 0);
    CHECK(nom::ws_status::failed == client.status());

    // A new attempt replaces the failed session and can still be canceled normally.
    io.restart();
    client.connect(callbacks);
    CHECK(nom::ws_status::connecting == client.status());
    client.close();
    io.run();
    CHECK(nom::ws_status::disconnected == client.status());
    CHECK(calls == 0);
}

TEST_CASE("Closing suppresses resolver cancellation allocation failure and queued events")
{
    STATIC_REQUIRE(noexcept(std::declval<nom::web_socket&>().close()));

    nom::init_logger(nom::main_logger);
    nom::asio::io_context io;
    nom::web_socket client(io.get_executor(), nom::url("wss://127.0.0.1:1/"));
    unsigned calls = 0;
    client.connect(nom::web_socket::delegate{[&](nom::web_socket&, const auto&) { ++calls; }});
    REQUIRE(nom::ws_status::connecting == client.status());

    {
        // Resolver cancellation allocates a fresh cancellation token after close() detaches the session.
        connection_allocation_failure failure(client, nom::ws_status::disconnected);
        client.close();
    }
    REQUIRE(failure_injected);
    CHECK(nom::ws_status::disconnected == client.status());
    CHECK(calls == 0);

    // Even if cancellation failed, the pending resolver completion must not reach the delegate.
    io.run();
    CHECK(calls == 0);
    client.close();
    CHECK(nom::ws_status::disconnected == client.status());
}
