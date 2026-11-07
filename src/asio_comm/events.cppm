// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

export module nom.asio_comm:events;

import std;

export namespace nom
{
    using network_timestamp = std::chrono::steady_clock::time_point;

    namespace event
    {
        struct on_connected
        {
            network_timestamp ts{};
        };

        struct on_disconnected
        {
            network_timestamp ts{};
            std::string reason;
        };

        struct ws_data
        {
            network_timestamp ts{};
            std::span<const char> data;

            std::string_view sv_data() const noexcept
            {
                return {data.data(), data.size()};
            }
        };

        struct network_error
        {
            network_timestamp ts{};
            std::string description;
            std::string message;
        };
    } // namespace event

} // namespace nom
