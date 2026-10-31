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
        static constexpr unsigned default_ping_interval_sec = 20;
        static constexpr unsigned default_socket_send_buf_size =
            64 * 1024; // Keep buffers modest to limit queuing latency.
        static constexpr unsigned default_socket_recv_buf_size = 64 * 1024;

        struct header
        {
            std::string name;
            std::string value;
        };

        // TODO IDs and stage definitions: ws_current_work.md.
        // TODO[WS-PARAMS] (stages WS-03, WS-04): Review parameter contracts and validation.
        // TODO[WS-PING] (stages WS-03, WS-06): Understand different types of pings and why we need
        // enable_outbound_ping.
        // TODO[WS-IDLE] (stages WS-03, WS-06): Review the idle timer and test it properly
        // (perhaps with a local WebSocket server).
        // TODO[WS-OP-TIMEOUT] (stages WS-03, WS-06): Review async operation timeouts;
        // reproduce a case when a timeout is triggered.
        // TODO[WS-LARGE-MESSAGES] (stages WS-03, WS-05): Test sending and receiving really big messages
        // (e.g. a huge Coinbase initial snapshot); distinguish socket buffers from message limits.

        std::vector<header> headers{};
        bool enable_outbound_ping{false};

        std::string agent_name{default_agent_name};
        unsigned async_operation_timeout_sec{default_async_operation_timeout_sec};
        unsigned ping_interval_sec{default_ping_interval_sec};
        unsigned socket_send_buf_size{default_socket_send_buf_size};
        unsigned socket_recv_buf_size{default_socket_recv_buf_size};
    };

    class web_socket;
    namespace detail
    {
        template <typename... Events> struct ws_events_delegate
        {
            using type = delegate<std::function<void(web_socket& ws, Events)>...>;
        };
    } // namespace detail

    // DONE TODO[WS-INIT] (stage WS-02): Introduce init (a session should start in init, not in disconnected).
    // DONE TODO[WS-FAILURE-NAME] (stage WS-02): Rename failed to conn_failed.
    // TODO[WS-TERMINAL-FAILURE] (stage WS-02): Make conn_failed terminal, on the same level as disconnected.
    // TODO[WS-DETACH] (stage WS-02): Any terminal state (disconnected, conn_failed) should also be
    // detached from the parent.
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
        ~web_socket();

        web_socket(const web_socket&) = delete;
        web_socket& operator=(const web_socket&) = delete;
        web_socket(web_socket&&) = delete;
        web_socket& operator=(web_socket&&) = delete;

        [[nodiscard]] const std::string& session_id() const noexcept;
        [[nodiscard]] ws_status status() const noexcept;
        [[nodiscard]] bool is_connected() const noexcept
        {
            return ws_status::connected == status();
        }

        void update_delegate(const delegate& callbacks)
        {
            delegate_ = callbacks;
        }
        [[nodiscard]] const delegate& callbacks() const noexcept
        {
            return delegate_;
        }

        ws_params& params() noexcept
        {
            return params_;
        }
        const ws_params& params() const noexcept
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
        delegate delegate_;
        std::unique_ptr<io_state> io_;
        std::shared_ptr<session> session_;
        ::nom::url url_;
        ws_params params_;
    };

} // namespace nom
