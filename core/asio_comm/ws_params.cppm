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
        // DEV-DONE/LIVE-TEST-PENDING TODO[WS-PING] (stage WS-06):
        // Enforce passive Ping and active Pong deadlines.

        // PARTIAL TODO[WS-OP-TIMEOUT] (stage WS-06):
        // Review async operation timeouts; reproduce a case when a timeout is triggered.
        struct server_ping_watchdog
        {
            // Deadline from handshake completion or the last server control Ping.
            // Expiry closes the transport and reports on_disconnected with a timeout reason.
            unsigned ping_timeout_sec = default_ping_timeout_sec;
        };

        struct client_ping_probe
        {
            // Delay before the first probe and after each completed probe (write followed by a control Pong).
            unsigned ping_period_sec = default_ping_period_sec;
            // Deadline from successful Ping write completion; async_operation_timeout_sec bounds the write.
            // Every control Pong is a probe event, regardless of payload. A Pong before write completion fails it.
            // Expiry closes the transport and reports on_disconnected.
            unsigned pong_timeout_sec = default_ping_timeout_sec;
        };
        // Only server control Pings refresh passive_ping; data and Pongs do not.
        // Active probes run one at a time, independently of passive_ping.
        std::optional<server_ping_watchdog> passive_ping{std::nullopt};
        std::optional<client_ping_probe> active_ping{std::nullopt};

        // Overall DNS/TCP/TLS/WebSocket connection budget, and the separate deadline for each active Ping write.
        unsigned async_operation_timeout_sec{default_async_operation_timeout_sec};
        [[nodiscard]] std::expected<void, std::string> validate() const;
    };

    std::ostream& operator<<(std::ostream& os, const ws_params& params);
} // namespace nom
