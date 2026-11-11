// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <catch2/catch_test_macros.hpp>

import std;
import nom.asio_comm;

using namespace std::chrono_literals;

static_assert(std::is_nothrow_move_constructible_v<nom::timer>);
static_assert(std::is_nothrow_move_assignable_v<nom::timer>);
static_assert(!std::is_copy_constructible_v<nom::timer>);
static_assert(!std::is_copy_assignable_v<nom::timer>);

TEST_CASE("Timer expiry is deferred and arm returns its deadline")
{
    boost::asio::io_context io;
    nom::timer clock(io.get_executor());
    unsigned calls{};
    const auto due = nom::timer::clock::now();
    CHECK(clock.arm_at(due,
                       [&]
                       {
                           ++calls;
                           CHECK_FALSE(clock.is_armed());
                           CHECK(nom::timer::clock::now() >= due);
                       }) == due);
    CHECK(clock.is_armed());
    CHECK(calls == 0);
    io.run();
    CHECK(calls == 1);
    CHECK_FALSE(clock.cancel());
}

TEST_CASE("Cancel and rearm invalidate queued successful completions even after the new deadline")
{
    boost::asio::io_context io;
    // Raw backend controls prove the expiry was queued: cancel() returns zero but the witness still runs.
    boost::asio::steady_timer gate(io), witness(io);
    nom::timer clock(io.get_executor());
    unsigned obsolete{}, current{};
    bool witness_succeeded{};
    const auto now = nom::timer::clock::now();
    gate.expires_at(now);
    gate.async_wait(
        [&](const boost::system::error_code& error)
        {
            REQUIRE_FALSE(error);
            // All three timers are already due. The reactor has queued successful completions.
            CHECK(witness.cancel() == 0);
            CHECK(clock.cancel());
            clock.arm_after(1ms, [&] { ++current; });
            std::this_thread::sleep_for(5ms);
        });
    clock.arm_at(now + 1ms, [&] { ++obsolete; });
    witness.expires_at(now + 2ms);
    witness.async_wait([&](const boost::system::error_code& error) { witness_succeeded = !error; });
    std::this_thread::sleep_for(5ms);
    io.run();
    CHECK(witness_succeeded);
    CHECK(obsolete == 0);
    CHECK(current == 1);
}

TEST_CASE("Timer replacement and cancellation release captures and suppress delivery")
{
    boost::asio::io_context io;
    nom::timer clock(io.get_executor());
    unsigned calls{};
    auto owner = std::make_shared<int>(1);
    const std::weak_ptr<int> weak = owner;
    clock.arm_after(1h, [owner] {});
    owner.reset();
    clock.arm_after(0ms, [&] { ++calls; });
    CHECK(weak.expired());
    CHECK(clock.cancel());
    CHECK_FALSE(clock.cancel());
    io.run();
    CHECK(calls == 0);
}

TEST_CASE("A timer can rearm and destroy itself from its callback")
{
    boost::asio::io_context io;
    auto clock = std::make_unique<nom::timer>(io.get_executor());
    unsigned calls{};
    clock->arm_after(0ms,
                     [&]
                     {
                         ++calls;
                         clock->arm_after(0ms,
                                          [&]
                                          {
                                              ++calls;
                                              clock.reset();
                                          });
                     });
    io.run();
    CHECK(calls == 2);
    CHECK_FALSE(clock);
}

TEST_CASE("Destroying an armed timer suppresses its callback while handlers drain")
{
    boost::asio::io_context io;
    unsigned calls{};
    {
        nom::timer clock(io.get_executor());
        clock.arm_after(0ms, [&] { ++calls; });
    }
    io.run();
    CHECK(calls == 0);
}

TEST_CASE("Invalid timer input preserves an existing wait and callback exceptions propagate")
{
    boost::asio::io_context io;
    nom::timer clock(io.get_executor());
    clock.arm_after(0ms, [] { throw std::runtime_error("callback"); });
    CHECK_THROWS_AS(clock.arm_after(-1ms, [] {}), std::invalid_argument);
    CHECK_THROWS_AS(clock.arm_after(nom::timer::clock::duration::max(), [] {}), std::invalid_argument);
    CHECK_THROWS_AS(clock.arm_after(0ms, {}), std::invalid_argument);
    CHECK(clock.is_armed());
    CHECK_THROWS_AS(io.run(), std::runtime_error);
    CHECK_FALSE(clock.is_armed());
}

TEST_CASE("Independent timers can cancel and rearm concurrently on separate execution contexts")
{
    constexpr unsigned worker_count = 4;
    constexpr unsigned iterations = 128;
    std::array<unsigned, worker_count> completed{}, obsolete{}, errors{};
    std::latch ready(worker_count);
    {
        std::vector<std::jthread> workers;
        for (unsigned worker = 0; worker < worker_count; ++worker)
            workers.emplace_back(
                [&, worker]
                {
                    boost::asio::io_context io;
                    nom::timer clock(io.get_executor());
                    ready.arrive_and_wait();
                    for (unsigned iteration = 0; iteration < iterations; ++iteration)
                    {
                        clock.arm_after(0ms, [&] { ++obsolete[worker]; });
                        clock.cancel();
                        clock.arm_after(0ms,
                                        [&]
                                        {
                                            ++completed[worker];
                                            if (clock.is_armed())
                                                ++errors[worker];
                                        });
                        io.restart();
                        io.run();
                    }
                });
    }
    for (unsigned worker = 0; worker < worker_count; ++worker)
    {
        CHECK(completed[worker] == iterations);
        CHECK(obsolete[worker] == 0);
        CHECK(errors[worker] == 0);
    }
}

TEST_CASE("Moving a pending timer preserves its callback after the source is destroyed")
{
    boost::asio::io_context io;
    std::optional<nom::timer> destination;
    unsigned calls{};
    {
        nom::timer source(io.get_executor());
        source.arm_after(0ms, [&] { ++calls; });
        destination.emplace(std::move(source));
        CHECK(destination->is_armed());
        CHECK_FALSE(source.is_armed());
        CHECK_FALSE(source.cancel());
        CHECK_THROWS_AS(source.arm_at(nom::timer::clock::now(), [] {}), std::logic_error);
        CHECK_THROWS_AS(source.arm_after(0ms, [] {}), std::logic_error);
    }
    io.run();
    CHECK(calls == 1);
    CHECK_FALSE(destination->is_armed());
}

TEST_CASE("Move assignment suppresses the destination's already queued successful completion")
{
    boost::asio::io_context io;
    // Keep the queue witness independent of nom::timer's logical cancellation.
    boost::asio::steady_timer gate(io), witness(io);
    nom::timer source(io.get_executor()), destination(io.get_executor());
    unsigned obsolete{}, current{};
    bool witness_succeeded{};
    auto owner = std::make_shared<int>(1);
    const std::weak_ptr<int> weak = owner;
    const auto now = nom::timer::clock::now();
    gate.expires_at(now);
    gate.async_wait(
        [&](const boost::system::error_code& error)
        {
            REQUIRE_FALSE(error);
            // The later witness also completed: both timer expiries are already queued.
            CHECK(witness.cancel() == 0);
            destination = std::move(source);
            CHECK(weak.expired());
            CHECK(destination.is_armed());
            CHECK_FALSE(source.is_armed());
        });
    destination.arm_at(now + 1ms, [&, owner] { ++obsolete; });
    owner.reset();
    source.arm_at(now + 2ms, [&] { ++current; });
    witness.expires_at(now + 3ms);
    witness.async_wait([&](const boost::system::error_code& error) { witness_succeeded = !error; });
    std::this_thread::sleep_for(5ms);
    io.run();
    CHECK(witness_succeeded);
    CHECK(obsolete == 0);
    CHECK(current == 1);
    CHECK_FALSE(destination.is_armed());
}

TEST_CASE("Moved-from timers can be reassigned and self-move preserves an active wait")
{
    boost::asio::io_context io;
    nom::timer first(io.get_executor());
    unsigned calls{};
    first.arm_after(0ms, [&] { ++calls; });
    nom::timer second(std::move(first));
    first = std::move(second);
    auto* alias = &first;
    first = std::move(*alias);
    CHECK(first.is_armed());
    CHECK_FALSE(second.cancel());
    io.run();
    CHECK(calls == 1);

    first.arm_after(0ms, [&] { ++calls; });
    first = std::move(second);
    CHECK_FALSE(first.is_armed());
    CHECK_FALSE(first.cancel());
    CHECK_THROWS_AS(first.arm_after(0ms, [] {}), std::logic_error);
    nom::timer empty(std::move(second));
    CHECK_FALSE(empty.is_armed());
    io.restart();
    io.run();
    CHECK(calls == 1);
}

TEST_CASE("Move assignment retains the source executor and the destination can cancel its wait")
{
    boost::asio::io_context source_io, destination_io;
    nom::timer source(source_io.get_executor()), destination(destination_io.get_executor());
    unsigned obsolete{}, current{};
    destination.arm_after(0ms, [&] { ++obsolete; });
    source.arm_after(0ms, [&] { ++current; });
    destination = std::move(source);
    destination_io.run();
    CHECK(obsolete == 0);
    CHECK(current == 0);
    source_io.run();
    CHECK(current == 1);

    destination.arm_after(0ms, [&] { ++current; });
    CHECK(destination.cancel());
    source_io.restart();
    source_io.run();
    CHECK(current == 1);
}
