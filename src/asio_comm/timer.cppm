// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

module;

#include <boost/asio/ts/netfwd.hpp>

export module nom.asio_comm:timer;

import std;

export namespace nom
{
    class timer
    {
        struct state;

    public:
        using clock = std::chrono::steady_clock;
        using callback = std::function<void()>;

        explicit timer(boost::asio::any_io_executor executor);
        timer(const timer&) = delete;
        timer& operator=(const timer&) = delete;
        timer(timer&& other) noexcept;
        timer& operator=(timer&& other) noexcept;
        ~timer();

        // Rearming cancels the previous wait. Past/current deadlines are dispatched asynchronously.
        clock::time_point arm_at(clock::time_point due, callback handler);
        clock::time_point arm_after(clock::duration delay, callback handler);
        // True if an armed generation was invalidated, even if its expiry was already queued.
        bool cancel() noexcept;
        [[nodiscard]] bool is_armed() const noexcept;

    private:
        std::shared_ptr<state> state_;
    };
} // namespace nom
