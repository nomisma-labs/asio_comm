// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/io_context.hpp>
#include <catch2/catch_test_macros.hpp>

import std;
import nom.asio_comm;

namespace https_cleanup_test
{
    thread_local bool fail_next_allocation{};
    thread_local bool failure_injected{};

    struct allocation_failure
    {
        allocation_failure()
        {
            failure_injected = false;
            fail_next_allocation = true;
        }

        ~allocation_failure()
        {
            fail_next_allocation = false;
        }
    };
} // namespace https_cleanup_test

// Exercise Asio's allocating resolver cancellation without a production failure-injection hook.
[[gnu::noinline]] void* operator new(std::size_t size)
{
    if (https_cleanup_test::fail_next_allocation)
    {
        https_cleanup_test::fail_next_allocation = false;
        https_cleanup_test::failure_injected = true;
        throw std::bad_alloc{};
    }
    if (void* memory = std::malloc(size ? size : 1))
        return memory;
    throw std::bad_alloc{};
}

// Keep Catch2's nothrow allocations paired with the replacement under ASan.
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

TEST_CASE("HTTPS close suppresses resolver cancellation allocation failure")
{
    STATIC_REQUIRE(noexcept(std::declval<nom::https&>().close()));
    boost::asio::io_context io;
    unsigned calls{};
    nom::https client(io.get_executor(), nom::url("https://localhost:1/"));
    client.connect(nom::https::https_delegate{[&](auto&&...) { ++calls; }});
    {
        https_cleanup_test::allocation_failure failure;
        client.close();
    }
    REQUIRE(https_cleanup_test::failure_injected);
    CHECK(client.status() == nom::https_status::conn_failed);
    // A failed physical cancellation must not permit the queued completion to notify the owner.
    io.run();
    CHECK(calls == 0);
    client.close();
    CHECK(client.status() == nom::https_status::conn_failed);
}

TEST_CASE("HTTPS destruction suppresses resolver cancellation allocation failure")
{
    boost::asio::io_context io;
    unsigned calls{};
    auto client = std::make_unique<nom::https>(io.get_executor(), nom::url("https://localhost:1/"));
    client->connect(nom::https::https_delegate{[&](auto&&...) { ++calls; }});
    {
        https_cleanup_test::allocation_failure failure;
        client.reset();
    }
    REQUIRE(https_cleanup_test::failure_injected);
    io.run();
    CHECK(calls == 0);
}

TEST_CASE("HTTPS move assignment suppresses replaced session cleanup failure")
{
    boost::asio::io_context io;
    unsigned calls{};
    nom::https source(io.get_executor(), nom::url("https://localhost:1/source"));
    nom::https destination(io.get_executor(), nom::url("https://localhost:1/destination"));
    destination.connect(nom::https::https_delegate{[&](auto&&...) { ++calls; }});
    const auto source_id = source.session_id();
    const auto replaced_id = destination.session_id();
    {
        https_cleanup_test::allocation_failure failure;
        destination = std::move(source);
    }
    REQUIRE(https_cleanup_test::failure_injected);
    CHECK(destination.session_id() == source_id);
    CHECK(destination.status() == nom::https_status::init);
    CHECK(source.session_id() == replaced_id);
    CHECK(source.status() == nom::https_status::conn_failed);
    io.run();
    CHECK(calls == 0);
}
