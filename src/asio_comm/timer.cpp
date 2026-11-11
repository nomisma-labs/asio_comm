// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0

module;

#include <spd_logger/logging.h>

#include <boost/asio/steady_timer.hpp>
#include <cstdio>

module nom.asio_comm;

import std;
import nom.core;

namespace nom
{
    struct timer::state
    {
        explicit state(boost::asio::any_io_executor executor) : wait(std::move(executor))
        {
        }

        static constexpr std::uint64_t invalid_generation = 0;
        inline static std::atomic<std::uint64_t> generation_counter{invalid_generation};

        boost::asio::steady_timer wait;
        // Zero denotes a disarmed timer; every arm obtains a fresh global generation.
        std::uint64_t generation{invalid_generation};
        callback handler;
    };

    timer::timer(boost::asio::any_io_executor executor) : state_(std::make_shared<state>(std::move(executor)))
    {
    }

    timer::timer(timer&& other) noexcept = default;

    timer& timer::operator=(timer&& other) noexcept
    {
        if (this != &other)
        {
            cancel();
            state_ = std::move(other.state_);
        }
        return *this;
    }

    timer::~timer()
    {
        cancel();
    }

    timer::clock::time_point timer::arm_after(clock::duration delay, callback handler)
    {
        const auto now = clock::now();
        if (delay < clock::duration::zero() || delay > clock::time_point::max() - now)
            throw std::invalid_argument("Timer delay must be nonnegative and fit the clock range");
        return arm_at(now + delay, std::move(handler));
    }

    timer::clock::time_point timer::arm_at(clock::time_point due, callback handler)
    {
        if (!state_)
            throw std::logic_error("Cannot arm a moved-from timer");
        if (!handler)
            throw std::invalid_argument("Timer callback must be set");
        auto generation = state::generation_counter.load(std::memory_order_relaxed);
        do
        {
            if (generation == std::numeric_limits<std::uint64_t>::max())
                throw std::overflow_error("Timer generation exhausted");
        } while (
            !state::generation_counter.compare_exchange_weak(generation, generation + 1, std::memory_order_relaxed));
        ++generation;

        cancel();
        try
        {
            state_->wait.expires_at(due);
            state_->handler = std::move(handler);
            state_->generation = generation;
            state_->wait.async_wait(
                [pending = state_, generation](const boost::system::error_code& error)
                {
                    // This check handles both:
                    // 1. Cancellation before expiry: pending->generation is invalid_generation.
                    // 2. Late cancellation of a queued successful expiry, too late for operation_aborted.
                    // If rearmed meanwhile, pending->generation holds a fresh ID instead. In either case,
                    // ignore the obsolete completion without touching the current wait or its callback.
                    if (pending->generation != generation)
                        return;
                    if (error)
                    {
                        // TODO: Replace this block with spd_logger's FATAL macro when available.
                        // Write the emergency diagnostic before attempting application logging.
                        char message[256];
                        const auto* description = error.message(message, sizeof(message));
                        std::fprintf(stderr,
                                     "Fatal timer completion error for generation %llu: %s:%d (%s)\n",
                                     static_cast<unsigned long long>(generation),
                                     error.category().name(),
                                     error.value(),
                                     description);
                        std::fflush(stderr);
                        try
                        {
                            LALWAYS("Fatal timer completion error for generation {}: {}:{} ({})",
                                    generation,
                                    error.category().name(),
                                    error.value(),
                                    description);
                        }
                        catch (...)
                        {
                            // Logging exceptions must not prevent flushing or termination.
                        }
                        flush_logger(main_logger);
                        std::terminate();
                    }
                    pending->generation = state::invalid_generation;
                    auto completion = std::move(pending->handler);
                    // Move the callable out before invoking it: it may rearm or destroy the timer.
                    completion();
                });
        }
        catch (...)
        {
            cancel();
            throw;
        }
        return due;
    }

    bool timer::cancel() noexcept
    {
        if (!is_armed())
            return false;

        // Invalidate before touching the backend; a queued successful expiry must also be suppressed.
        state_->generation = state::invalid_generation;
        state_->handler = {};
        try
        {
            state_->wait.cancel();
        }
        catch (...)
        {
            // Logical cancellation still holds; destruction/cancel must not invoke user code or throw.
        }
        return true;
    }

    bool timer::is_armed() const noexcept
    {
        return state_ && state_->generation != state::invalid_generation;
    }
} // namespace nom
