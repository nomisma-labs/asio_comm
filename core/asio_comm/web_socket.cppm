// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

module;

#include <base/util/enum.h>
// Consumers include their own Asio headers; full backend definitions stay in the implementation unit.
#include <boost/asio/ts/netfwd.hpp>

export module nom.asio_comm;

import std;
export import nom.url;
export import nom.assembly;

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
        // TODO[WS-IDLE] (stage WS-06): Review the idle timer and test it properly
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

    class web_socket;
    namespace detail
    {
        template <typename... Events> struct ws_events_delegate
        {
            using type = delegate<std::function<void(web_socket& ws, Events)>...>;
        };
    } // namespace detail

    NOM_ENUM(ws_status, std::uint8_t, init, connecting, connected, disconnecting, conn_failed, disconnected)

    // TODO[WS-CABLE-LOSS] (stage WS-06): Reproduce hard disconnection (unplug the network cable).
    // TODO[WS-INTERFACE-LOSS] (stage WS-06): Reproduce Docker-based interface disconnection
    // (a soft disconnection of a virtual interface may only be caught by the idle timer).
    // TODO[WS-PEER-CLOSE] (stage WS-06): Reproduce normal server disconnection
    // (e.g. Binance resets the MD connection once per day).
    // TODO[WS-EXCEPTIONS] (stage WS-08): Consider making the interface noexcept for everything
    // except the constructor.

    class web_socket
    {
        struct io_state;
        struct session;

    public:
        using delegate = ::nom::detail::ws_events_delegate<const event::ws_data&,
                                                           const event::on_connected&,
                                                           const event::on_disconnected&,
                                                           const event::network_error&>::type;

        web_socket(boost::asio::any_io_executor executor, const ::nom::url& address);
        web_socket(boost::asio::any_io_executor executor,
                   std::string_view host,
                   std::uint16_t port,
                   std::string_view target);
        web_socket(const web_socket&) = delete;
        web_socket& operator=(const web_socket&) = delete;
        web_socket(web_socket&&);
        web_socket& operator=(web_socket&&) noexcept;
        ~web_socket();

        [[nodiscard]] const std::string& session_id() const noexcept;
        [[nodiscard]] ws_status status() const noexcept;
        [[nodiscard]] bool is_connected() const noexcept
        {
            return ws_status::connected == status();
        }

        void update_delegate(const delegate& callbacks);
        [[nodiscard]] const delegate& callbacks() const noexcept;

        void update_params(const ws_params& params);
        [[nodiscard]] const ws_params& params() const noexcept
        {
            return params_;
        }

        void connect(const delegate& callbacks);
        void close() noexcept;
        [[nodiscard]] bool send(const std::string& text);

        [[nodiscard]] const ::nom::url& url() const noexcept
        {
            return url_;
        }

        friend std::ostream& operator<<(std::ostream& os, const web_socket& ws)
        {
            return os << ws.session_id();
        }

    private:
        void swap_state(web_socket& other) noexcept;

        ws_params params_;
        ::nom::url url_;
        std::unique_ptr<io_state> io_;
        std::shared_ptr<session> session_;
    };

} // namespace nom
