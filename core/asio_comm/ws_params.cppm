// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

export module nom.asio_comm:ws_params;

import std;

export namespace nom
{
    struct ws_params
    {
        static constexpr char default_agent_name[] = "nomisma/asio_comm";
        static constexpr unsigned default_async_operation_timeout_sec = 30;
        static constexpr unsigned default_ping_period_sec = 20;
        static constexpr unsigned default_ping_timeout_sec = 20;
        static constexpr unsigned default_receive_data_timeout_sec = 20;
        static constexpr unsigned default_socket_send_buf_size = 64 * 1024;
        static constexpr unsigned default_socket_recv_buf_size = 64 * 1024;
        static constexpr unsigned default_max_message_size = 16 * 1024 * 1024;

        struct header
        {
            std::string name;
            std::string value;
        };

        std::string agent_name{default_agent_name};

        std::vector<header> headers{};

        // Size settings are in bytes and take effect when connecting:
        // - socket_send_buf_size requests the kernel TCP send buffer (SO_SNDBUF).
        //   Larger buffers allow more queued data; send() can still block.
        // - socket_recv_buf_size requests the kernel TCP receive buffer (SO_RCVBUF).
        //   Complete WebSocket messages can be larger than this buffer.
        // Both socket sizes must be in [1, INT_MAX]. The OS may adjust or reject
        // the request; neither setting limits WebSocket frame or message sizes.
        // - max_message_size is a positive limit for one complete incoming message,
        //   including all its fragments. Oversized input fails without delivering
        //   a partial ws_data event. It does not limit outgoing messages.
        // The message limit reserves no memory up front. The application's
        // contiguous receive buffer grows on demand and retains capacity after
        // delivery. Capacity and reallocation peaks can exceed the payload size;
        // max_message_size is not a total memory budget.
        unsigned socket_send_buf_size{default_socket_send_buf_size};
        unsigned socket_recv_buf_size{default_socket_recv_buf_size};
        unsigned max_message_size{default_max_message_size};

        // TODO IDs and stage definitions: ws_current_work.md.
        // TODO[WS-PING] (stage WS-06): Enforce passive Ping and active Pong deadlines.
        // TODO[WS-IDLE] (stage WS-06): Review the receive watchdog timer and test it properly
        // (perhaps with a local WebSocket server).
        // TODO[WS-OP-TIMEOUT] (stage WS-06): Review async operation timeouts;
        // reproduce a case when a timeout is triggered.
        struct server_ping_watchdog
        {
            unsigned ping_timeout_sec = default_ping_timeout_sec;
        };

        struct client_ping_probe
        {
            unsigned ping_period_sec = default_ping_period_sec;
            unsigned pong_timeout_sec = default_ping_timeout_sec;
        };
        // Ping/Pong timeout values are validated but their enforcement is pending WS-PING.
        // active_ping currently enables periodic Pings and counts received Pongs as activity.
        std::optional<server_ping_watchdog> passive_ping{std::nullopt};
        std::optional<client_ping_probe> active_ping{std::nullopt};

        unsigned async_operation_timeout_sec{default_async_operation_timeout_sec};
        // Disabled by default. An enabled watchdog is independent of the Ping settings.
        // Current mixed-activity policy: sample every N + N/2 seconds and close when whole
        // elapsed seconds exceed N + N/5. Rounding, grace and useful-data semantics await WS-IDLE.
        struct receive_data_watchdog
        {
            unsigned timeout_sec{default_receive_data_timeout_sec};
        };
        std::optional<receive_data_watchdog> receive_watchdog{std::nullopt};

        [[nodiscard]] std::expected<void, std::string> validate() const;
    };

    std::ostream& operator<<(std::ostream& os, const ws_params& params);
} // namespace nom
