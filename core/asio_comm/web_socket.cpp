// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

module;

#include <spd_logger/logging.h>

#include <boost/asio/compose.hpp>
#include <boost/asio/ssl/host_name_verification.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <cassert>

module nom.asio_comm;

import std;
import nom.spd_logger;

namespace nom
{
    namespace net = boost::asio;
    namespace ssl = boost::asio::ssl;
    namespace beast = boost::beast;
    namespace http = beast::http;
    namespace websocket = beast::websocket;
    using tcp = boost::asio::ip::tcp;

    namespace
    {
        struct ws_tls_stream : beast::ssl_stream<beast::tcp_stream>
        {
            using base_type = beast::ssl_stream<beast::tcp_stream>;
            using base_type::base_type;

            beast::error_code teardown_error;
        };

        struct tls_teardown_op
        {
            ws_tls_stream& stream;
            beast::role_type role;

            template <typename Self> void operator()(Self& self)
            {
                self.reset_cancellation_state(net::enable_total_cancellation());
                beast::async_teardown(role, static_cast<ws_tls_stream::base_type&>(stream), std::move(self));
            }

            template <typename Self> void operator()(Self& self, beast::error_code ec)
            {
                // Match Beast's treatment of EOF during TLS shutdown.
                stream.teardown_error = ec == net::error::eof ? beast::error_code{} : ec;
                // Let Beast return its WebSocket result; report the TLS error separately.
                self.complete(beast::error_code{});
            }
        };

        template <typename Handler> auto async_teardown(beast::role_type role, ws_tls_stream& stream, Handler&& handler)
        {
            return net::async_compose<Handler, void(beast::error_code)>(tls_teardown_op{stream, role}, handler, stream);
        }

        bool is_header_name(std::string_view name) noexcept
        {
            return !name.empty() && std::ranges::all_of(name,
                                                        [](unsigned char c)
                                                        {
                                                            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                                                                   (c >= '0' && c <= '9') ||
                                                                   std::string_view{"!#$%&'*+-.^_`|~"}.find(
                                                                       static_cast<char>(c)) != std::string_view::npos;
                                                        });
        }

        bool is_header_value(std::string_view value) noexcept
        {
            return std::ranges::all_of(value, [](unsigned char c) { return c == '\t' || (c >= 0x20 && c != 0x7f); });
        }

        bool is_handshake_header(std::string_view name) noexcept
        {
            switch (http::string_to_field({name.data(), name.size()}))
            {
            case http::field::host:
            case http::field::connection:
            case http::field::upgrade:
            case http::field::sec_websocket_key:
            case http::field::sec_websocket_version:
            case http::field::sec_websocket_accept:
            case http::field::sec_websocket_extensions:
            case http::field::content_length:
            case http::field::transfer_encoding:
                return true;
            default:
                return false;
            }
        }
    } // namespace

    std::expected<void, std::string> ws_params::validate() const
    {
        if (async_operation_timeout_sec == 0)
            return std::unexpected("/async_operation_timeout_sec must be positive");
        if (receive_watchdog && receive_watchdog->timeout_sec == 0)
            return std::unexpected("/receive_watchdog/timeout_sec must be positive");
        if (passive_ping && passive_ping->ping_timeout_sec == 0)
            return std::unexpected("/passive_ping/ping_timeout_sec must be positive");
        if (active_ping)
        {
            if (active_ping->ping_period_sec == 0)
                return std::unexpected("/active_ping/ping_period_sec must be positive");
            if (active_ping->pong_timeout_sec == 0)
                return std::unexpected("/active_ping/pong_timeout_sec must be positive");
        }
        constexpr auto max_buffer_size = static_cast<unsigned>(std::numeric_limits<int>::max());
        if (socket_send_buf_size == 0 || socket_send_buf_size > max_buffer_size)
            return std::unexpected("/socket_send_buf_size must be in [1, INT_MAX]");
        if (socket_recv_buf_size == 0 || socket_recv_buf_size > max_buffer_size)
            return std::unexpected("/socket_recv_buf_size must be in [1, INT_MAX]");
        if (max_message_size == 0)
            return std::unexpected("/max_message_size must be positive");
        if (agent_name.find_first_not_of(" \t") == std::string::npos)
            return std::unexpected("/agent_name must be nonempty");
        if (agent_name.size() > http::fields::max_value_size)
            return std::unexpected("/agent_name exceeds the supported HTTP field length");
        if (!is_header_value(agent_name))
            return std::unexpected("/agent_name contains an invalid HTTP field character");

        for (std::size_t i = 0; i < headers.size(); ++i)
        {
            const auto& header = headers[i];
            const auto path = "/headers/" + std::to_string(i);
            if (!is_header_name(header.name))
                return std::unexpected(path + "/name must be a nonempty HTTP token");
            if (header.name.size() > http::fields::max_name_size)
                return std::unexpected(path + "/name exceeds the supported HTTP field length");
            if (is_handshake_header(header.name))
                return std::unexpected(path + "/name is reserved for the WebSocket handshake");
            if (header.value.size() > http::fields::max_value_size)
                return std::unexpected(path + "/value exceeds the supported HTTP field length");
            if (!is_header_value(header.value))
                return std::unexpected(path + "/value contains an invalid HTTP field character");
            for (std::size_t j = 0; j < i; ++j)
                if (beast::iequals(header.name, headers[j].name))
                    return std::unexpected(path + "/name duplicates an earlier header");
        }
        return {};
    }

    struct web_socket::io_state
    {
        explicit io_state(net::any_io_executor executor) : executor(std::move(executor))
        {
        }

        net::any_io_executor executor;
        ssl::context ssl_ctx{ssl::context::tls_client};
    };

    struct web_socket::session : std::enable_shared_from_this<session>
    {
        friend void web_socket::swap_state(web_socket& other) noexcept;

    private:
        inline static std::atomic<std::uint64_t> next_id{0};

        struct setup_state
        {
            explicit setup_state(const net::any_io_executor& executor) : resolver(executor)
            {
            }

            tcp::resolver resolver;
            std::string host_header;
        };

        template <ws_status NewStatus> void set_status() noexcept
        {
            static_assert(NewStatus != ws_status::init, "Only construction may select init");
            assert(static_cast<std::uint8_t>(status_) < static_cast<std::uint8_t>(NewStatus) &&
                   "State transitions must change the state in increasing order");
            status_ = NewStatus;
            if constexpr (NewStatus == ws_status::connected || NewStatus == ws_status::conn_failed)
                setup_.reset();
        }

    public:
        session(web_socket* parent)
            : parent_(parent),
              id_{[this]()
                  {
                      assert(parent_);
                      std::ostringstream strm;
                      strm << "WS[" << parent_->url_ << ':' << next_id.fetch_add(1, std::memory_order_relaxed) << ']';
                      return strm.str();
                  }()},
              stream_(parent_->io_->executor, parent_->io_->ssl_ctx)
        {
        }

        const std::string& id() const noexcept
        {
            return id_;
        }

        void update_delegate(const delegate& callbacks)
        {
            delegate_ = callbacks;
        }

        const delegate& callbacks() const noexcept
        {
            return delegate_;
        }

        // A session may outlive its parent while canceled operations complete.
        void close() noexcept
        {
            switch (status_)
            {
            case ws_status::init:
                // No operations to cancel; keep the session ready for its first connect().
                return;
            case ws_status::connecting:
                set_status<ws_status::conn_failed>();
                break;
            case ws_status::connected:
            case ws_status::disconnecting:
                set_status<ws_status::disconnected>();
                break;
            case ws_status::conn_failed:
            case ws_status::disconnected:
                // Repeated close preserves the terminal outcome.
                break;
            default:
                assert(false && "Close requires a valid session state");
                return;
            }

            // Cleanup is best effort. Keep each attempt independent, without logging or delegate calls.
            try
            {
                // The retained stream must not keep Beast's handshake/close timer alive after abort.
                stream_.set_option(websocket::stream_base::timeout{.handshake_timeout = websocket::stream_base::none(),
                                                                   .idle_timeout = websocket::stream_base::none(),
                                                                   .keep_alive_pings = false});
            }
            catch (...)
            {
            }
            try
            {
                beast::error_code ignored;
                beast::get_lowest_layer(stream_).socket().close(ignored);
            }
            catch (...)
            {
            }
            try
            {
                if (receive_watchdog_timer_)
                    receive_watchdog_timer_->cancel();
            }
            catch (...)
            {
            }
            try
            {
                if (active_ping_timer_)
                    active_ping_timer_->cancel();
            }
            catch (...)
            {
            }
        }

        ws_status status() const noexcept
        {
            return status_;
        }

        // TODO IDs and stage definitions: ws_current_work.md.
        // TODO[WS-CONNECT-DEADLINE] (stage WS-06): Make the connection procedure time-bound
        // (we can use default_async_operation_timeout_sec for it).
        // TODO[WS-TRANSPORT-OPTIONS] (stage WS-09): Test various connection options
        // (different TLS levels, no SSL?).
        void connect()
        {
            switch (status_)
            {
            case ws_status::init:
            {
                set_status<ws_status::connecting>();

                try
                {
                    // Parameters are fixed for this attempt; disabled policies need no timer objects.
                    if (parent_->params_.receive_watchdog)
                        receive_watchdog_timer_.emplace(parent_->io_->executor);
                    if (parent_->params_.active_ping)
                        active_ping_timer_.emplace(parent_->io_->executor);
                    setup_ = std::make_unique<setup_state>(parent_->io_->executor);
                    setup_->resolver.async_resolve(parent_->url_.host(),
                                                   std::to_string(parent_->url_.port()),
                                                   beast::bind_front_handler(&session::on_resolve, shared_from_this()));
                }
                catch (...)
                {
                    set_status<ws_status::conn_failed>();
                    throw;
                }
                break;
            }
            default:
                LERROR("{} Can't connect in state: {}", id(), enum_to_string(status_));
                break;
            }
        }

    private:
        // Connection stages run sequentially; close() leaves connecting before cancelling operations.
        void on_resolve(beast::error_code ec, tcp::resolver::results_type endpoints)
        {
            switch (status_)
            {
            case ws_status::connecting:
            {
                const auto notify_error = [this](std::string_view description, std::string_view message)
                {
                    delegate_(*parent_,
                              event::network_error{.ts = std::chrono::steady_clock::now(),
                                                   .description = std::string(description),
                                                   .message = std::string(message)});
                };
                // connect() entered connecting and started DNS resolution.
                if (ec)
                {
                    set_status<ws_status::conn_failed>();
                    LERROR("{} Resolve error: {}", id(), ec.message());
                    notify_error("Resolve error", ec.message());
                    break;
                }

                try
                {
                    beast::get_lowest_layer(stream_).expires_after(
                        std::chrono::seconds(parent_->params_.async_operation_timeout_sec));
                    beast::get_lowest_layer(stream_).async_connect(
                        endpoints, beast::bind_front_handler(&session::on_connect, shared_from_this()));
                }
                catch (const std::exception& error)
                {
                    set_status<ws_status::conn_failed>();
                    notify_error("TCP connect setup error", error.what());
                }
                catch (...)
                {
                    set_status<ws_status::conn_failed>();
                    notify_error("TCP connect setup error", "Unknown exception");
                }

                break;
            }
            case ws_status::conn_failed:
                // close() (including reconnect/destruction) abandoned this resolution before completion.
                break;
            default:
                assert(false && "Resolve completions require connecting or conn_failed state");
                LERROR("{} Unexpected state: {}", id(), enum_to_string(status_));
                break;
            }
        }

        void on_connect(beast::error_code ec, tcp::resolver::results_type::endpoint_type ep)
        {
            switch (status_)
            {
            case ws_status::connecting:
            {
                const auto notify_error = [this](std::string_view description, std::string_view message)
                {
                    delegate_(*parent_,
                              event::network_error{.ts = std::chrono::steady_clock::now(),
                                                   .description = std::string(description),
                                                   .message = std::string(message)});
                };
                // on_resolve() started TCP connect while the connection attempt remained active.
                if (ec)
                {
                    set_status<ws_status::conn_failed>();
                    LERROR("{} Connect error: {}", id(), ec.message());
                    notify_error("Connect error", ec.message());
                    break;
                }

                if (!SSL_set_tlsext_host_name(stream_.next_layer().native_handle(), parent_->url_.host().c_str()))
                {
                    set_status<ws_status::conn_failed>();
                    ec = beast::error_code(static_cast<int>(::ERR_get_error()), net::error::get_ssl_category());
                    LERROR("{} Connect error: {}", id(), ec.message());
                    notify_error("Connect/SSL error", ec.message());
                    break;
                }

                try
                {
                    apply_socket_options();

                    // The HTTP Host header brackets IPv6 literals; DNS and TLS use the unbracketed host.
                    const auto& host = parent_->url_.host();
                    setup_->host_header = (std::string::npos == host.find(':') ? host : '[' + host + ']') + ':' +
                                          std::to_string(ep.port());
                    stream_.next_layer().set_verify_callback(ssl::host_name_verification(host));
                    stream_.next_layer().async_handshake(
                        ssl::stream_base::client,
                        beast::bind_front_handler(&session::on_ssl_handshake, shared_from_this()));
                }
                catch (const std::exception& error)
                {
                    set_status<ws_status::conn_failed>();
                    notify_error("TLS handshake setup error", error.what());
                }
                catch (...)
                {
                    set_status<ws_status::conn_failed>();
                    notify_error("TLS handshake setup error", "Unknown exception");
                }
                break;
            }
            case ws_status::conn_failed:
                // close() (including reconnect/destruction) abandoned TCP connect before completion.
                break;
            default:
                assert(false && "TCP connect completions require connecting or conn_failed state");
                LERROR("{} Unexpected state: {}", id(), enum_to_string(status_));
                break;
            }
        }

        void on_ssl_handshake(beast::error_code ec)
        {
            switch (status_)
            {
            case ws_status::connecting:
            {
                const auto notify_error = [this](std::string_view description, std::string_view message)
                {
                    delegate_(*parent_,
                              event::network_error{.ts = std::chrono::steady_clock::now(),
                                                   .description = std::string(description),
                                                   .message = std::string(message)});
                };
                // on_connect() completed TCP/SNI setup and started the TLS handshake.
                if (ec)
                {
                    set_status<ws_status::conn_failed>();
                    LERROR("{} Connect/SSL handshake error: {}", id(), ec.message());
                    notify_error("Connect/SSL handshake error", ec.message());
                    break;
                }

                try
                {
                    // Beast's WebSocket timeout takes over after the TLS handshake.
                    beast::get_lowest_layer(stream_).expires_never();
                    stream_.set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));
                    stream_.read_message_max(parent_->params_.max_message_size);
                    stream_.set_option(websocket::stream_base::decorator(
                        [this](websocket::request_type& req)
                        {
                            assert(ws_status::connecting == status_);
                            req.set(http::field::user_agent, parent_->params_.agent_name);
                            for (const auto& header : parent_->params_.headers)
                                req.set(header.name, header.value);
                        }));
                    stream_.async_handshake(setup_->host_header,
                                            parent_->url_.target(),
                                            beast::bind_front_handler(&session::on_handshake, shared_from_this()));
                }
                catch (const std::exception& error)
                {
                    set_status<ws_status::conn_failed>();
                    notify_error("WebSocket handshake setup error", error.what());
                }
                catch (...)
                {
                    set_status<ws_status::conn_failed>();
                    notify_error("WebSocket handshake setup error", "Unknown exception");
                }
                break;
            }
            case ws_status::conn_failed:
                // close() (including reconnect/destruction) abandoned the TLS handshake
                // before completion.
                break;
            default:
                assert(false && "TLS handshake completions require connecting or conn_failed state");
                LERROR("{} Unexpected state: {}", id(), enum_to_string(status_));
                break;
            }
        }

        void apply_socket_options()
        {
            assert(ws_status::connecting == status_);
            auto& socket = beast::get_lowest_layer(stream_).socket();

            beast::error_code ec;

            socket.set_option(net::socket_base::keep_alive(true), ec);
            if (ec)
                LWARN("{} SO_KEEPALIVE set error: {}", id(), ec.message());

            socket.set_option(tcp::no_delay(true), ec);
            if (ec)
                LWARN("{} TCP_NODELAY set error: {}", id(), ec.message());

            socket.set_option(net::socket_base::send_buffer_size(parent_->params_.socket_send_buf_size), ec);
            if (ec)
                LWARN("{} SO_SNDBUF set error: {}", id(), ec.message());

            socket.set_option(net::socket_base::receive_buffer_size(parent_->params_.socket_recv_buf_size), ec);
            if (ec)
                LWARN("{} SO_RCVBUF set error: {}", id(), ec.message());
        }

        void on_handshake(beast::error_code ec)
        {
            switch (status_)
            {
            case ws_status::connecting:
            {
                const auto notify_error = [this](std::string_view description, std::string_view message)
                {
                    delegate_(*parent_,
                              event::network_error{.ts = std::chrono::steady_clock::now(),
                                                   .description = std::string(description),
                                                   .message = std::string(message)});
                };
                // on_ssl_handshake() completed TLS setup and started the WebSocket handshake.
                try
                {
                    beast::get_lowest_layer(stream_).expires_never();
                }
                catch (const std::exception& error)
                {
                    set_status<ws_status::conn_failed>();
                    notify_error("WebSocket handshake completion error", error.what());
                    break;
                }
                catch (...)
                {
                    set_status<ws_status::conn_failed>();
                    notify_error("WebSocket handshake completion error", "Unknown exception");
                    break;
                }
                if (ec)
                {
                    set_status<ws_status::conn_failed>();
                    LERROR("{} Connect/Handshake error: {}", id(), ec.message());
                    notify_error("Connect/Handshake error", ec.message());
                    break;
                }
                last_recv_ts_ = std::chrono::steady_clock::now();
                LDEBUG("{} connected", id());
                set_status<ws_status::connected>();
                delegate_(*parent_, event::on_connected{.ts = last_recv_ts_});
                // The delegate may call close() or trigger a write-failure shutdown before receive/timer work starts.
                if (ws_status::connected != status_)
                    break;

                if (active_ping_timer_)
                {
                    LDEBUG("{} Enable outbound ping", id());
                    start_ping_timer();
                }

                // The stream owns this callback; pending operations keep the session alive.
                stream_.control_callback(beast::bind_front_handler(&session::on_control, this));

                stream_.async_read(buffer_, beast::bind_front_handler(&session::on_receive, shared_from_this()));
                if (receive_watchdog_timer_)
                    start_receive_watchdog_timer();
                break;
            }
            case ws_status::conn_failed:
                // close() (including reconnect/destruction) abandoned the WebSocket handshake before completion.
                break;
            default:
                assert(false && "WebSocket handshake completions require connecting or conn_failed state");
                LERROR("{} Unexpected state: {}", id(), enum_to_string(status_));
                break;
            }
        }

        void on_control(websocket::frame_type kind, [[maybe_unused]] std::string_view payload)
        {
            switch (status_)
            {
            // on_handshake() installed the callback and started reading while connected.
            case ws_status::connected:
            // shutdown() started closing while pending operations can still process control frames.
            case ws_status::disconnecting:
            {
                switch (kind)
                {
                case websocket::frame_type::ping:
                {
                    last_recv_ts_ = std::chrono::steady_clock::now();
                    LDEBUG("{} Ping request received from server", id());
                    break;
                }
                case websocket::frame_type::pong:
                {
                    if (parent_->params_.active_ping)
                        last_recv_ts_ = std::chrono::steady_clock::now();
                    LDEBUG("{} Pong response received from server", id());
                    break;
                }
                case websocket::frame_type::close:
                {
                    LDEBUG("{} Close request received from server", id());
                    if (receive_watchdog_timer_)
                        receive_watchdog_timer_->cancel();
                    set_status<ws_status::disconnected>();
                    delegate_(
                        *parent_,
                        event::on_disconnected{.ts = std::chrono::steady_clock::now(), .reason = "Closed by server"});
                    break;
                }
                }
                break;
            }
            case ws_status::disconnected:
                // close(), a previous peer close, a read error, or on_shutdown() disconnected this session.
                break;
            default:
                assert(false && "Control callbacks require connected, disconnecting, or disconnected state");
                LERROR("{} Unexpected state: {}", id(), enum_to_string(status_));
                break;
            }
        }

        void start_receive_watchdog_timer()
        {
            assert(receive_watchdog_timer_ && parent_->params_.receive_watchdog);
            const auto receive_watchdog_timeout_sec = parent_->params_.receive_watchdog->timeout_sec;
            const auto receive_watchdog_grace_period_sec = receive_watchdog_timeout_sec / 2;
            receive_watchdog_timer_->expires_after(std::chrono::seconds{receive_watchdog_timeout_sec} +
                                                   std::chrono::seconds{receive_watchdog_grace_period_sec});
            receive_watchdog_timer_->async_wait(
                beast::bind_front_handler(&session::on_receive_watchdog_timer, shared_from_this()));
        }

        void start_ping_timer()
        {
            assert(active_ping_timer_ && parent_->params_.active_ping);
            active_ping_timer_->expires_after(std::chrono::seconds{parent_->params_.active_ping->ping_period_sec});
            active_ping_timer_->async_wait(beast::bind_front_handler(&session::on_ping_timer, shared_from_this()));
        }

        void on_ping_timer(beast::error_code ec)
        {
            switch (status_)
            {
            case ws_status::connected:
            {
                // on_handshake() starts the first timer; each ping completion rearms it.
                if (ec)
                {
                    if (net::error::operation_aborted == ec)
                        break;
                    LWARN("{} Ping timer error: {}", id(), ec.message());
                    break;
                }

                LDEBUG("{} Sending ping to server", id());
                stream_.async_ping({}, beast::bind_front_handler(&session::on_ping, shared_from_this()));
                break;
            }
            // shutdown() canceled the pending wait; an expiry may already have been queued.
            case ws_status::disconnecting:
            // close(), peer close, a read error, or on_shutdown() disconnected the session.
            // A peer close/read error may leave the ping timer running until it expires.
            case ws_status::disconnected:
                if (ec && net::error::operation_aborted != ec)
                    LWARN("{} Ping timer error: {}", id(), ec.message());
                break;
            default:
                assert(false && "Ping timer completions require connected, disconnecting, or disconnected state");
                LERROR("{} Unexpected state: {}", id(), enum_to_string(status_));
                break;
            }
        }

        void on_ping(beast::error_code ec)
        {
            switch (status_)
            {
            case ws_status::connected:
                // on_ping_timer() started this send, and no disconnection has started since.
                if (ec)
                    LWARN("{} Ping error: {}", id(), ec.message());
                else
                    LDEBUG("{} sent ping to server", id());

                // Wait for this send to finish before scheduling another ping.
                start_ping_timer();
                break;
            case ws_status::disconnecting:
                // shutdown() started closing while this ping completion was pending.
                break;
            case ws_status::disconnected:
                // close(), a peer close, a read error, or on_shutdown() disconnected this session.
                break;
            default:
                assert(false && "Ping completions require connected, disconnecting, or disconnected state");
                LERROR("{} Unexpected state: {}", id(), enum_to_string(status_));
                break;
            }
        }

        void on_receive_watchdog_timer(beast::error_code ec)
        {
            switch (status_)
            {
            case ws_status::connected:
            {
                // on_handshake() started the receive watchdog timer; subsequent healthy expiries rearm it.
                if (ec)
                {
                    if (net::error::operation_aborted == ec)
                        break;

                    LWARN("{} Receive watchdog timer error: {}", id(), ec.message());
                    shutdown();
                    break;
                }

                assert(parent_->params_.receive_watchdog);
                const auto receive_watchdog_timeout_sec = parent_->params_.receive_watchdog->timeout_sec;
                const auto receive_watchdog_tolerance_sec = receive_watchdog_timeout_sec / 5;

                const auto diff_ts =
                    std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - last_recv_ts_)
                        .count();

                if (const bool receive_watchdog_expired =
                        std::chrono::seconds{diff_ts} > std::chrono::seconds{receive_watchdog_timeout_sec} +
                                                            std::chrono::seconds{receive_watchdog_tolerance_sec};
                    receive_watchdog_expired)
                {
                    LWARN(
                        "{} Receive watchdog timer expired: no tracked receive activity for {} seconds", id(), diff_ts);
                    shutdown();
                    break;
                }

                start_receive_watchdog_timer();
                break;
            }
            case ws_status::disconnecting:
                // shutdown() canceled the pending wait; an already queued expiry is ignored too.
                break;
            case ws_status::disconnected:
                // close() (including reconnect/destruction), peer close, a read error, or on_shutdown()
                // disconnected the session before this canceled or already queued wait completed.
                break;
            default:
                // The receive watchdog timer starts only after a successful handshake; sessions are never reused.
                assert(false &&
                       "Receive watchdog timer completions require connected, disconnecting, or disconnected state");
                LERROR("{} Unexpected state: {}", id(), enum_to_string(status_));
                break;
            }
        }

        std::string disconnect_reason(beast::error_code ec) const
        {
            std::string reason;
            if (ec == websocket::error::message_too_big)
                reason = "Incoming WebSocket message exceeds max_message_size=" +
                         std::to_string(parent_->params_.max_message_size) + " bytes";
            else if (ec)
                reason = ec.message();

            if (const auto tls_error = stream_.next_layer().teardown_error)
            {
                if (!reason.empty())
                    reason += "; ";
                reason += "TLS shutdown error: " + tls_error.message();
            }
            return reason;
        }

        void on_receive(beast::error_code ec, [[maybe_unused]] std::size_t bytes_transferred)
        {
            switch (status_)
            {
            // on_handshake() set connected and started the read loop; no close has started since.
            case ws_status::connected:
            // shutdown() (receive watchdog timer or write failure) ran before this read completion.
            // A previous event::ws_data callback can also trigger shutdown(), then rearm the read below.
            case ws_status::disconnecting:
            {
                if (ec)
                {
                    if (receive_watchdog_timer_)
                        receive_watchdog_timer_->cancel();
                    set_status<ws_status::disconnected>();
                    delegate_(*parent_,
                              event::on_disconnected{.ts = std::chrono::steady_clock::now(),
                                                     .reason = disconnect_reason(ec)});
                    break;
                }

                const auto mutable_buffer = buffer_.data(); // Non-owning view; no payload copy.
                last_recv_ts_ = std::chrono::steady_clock::now();

                delegate_(*parent_,
                          event::ws_data{.ts = last_recv_ts_,
                                         .data = std::span<const char>(static_cast<char*>(mutable_buffer.data()),
                                                                       mutable_buffer.size())});
                buffer_.consume(buffer_.size());

                // The delegate may close or replace this session; do not rearm after disconnection.
                if (ws_status::disconnected == status_)
                    break;

                stream_.async_read(buffer_, beast::bind_front_handler(&session::on_receive, shared_from_this()));
                break;
            }
            case ws_status::disconnected:
                // close() (including reconnect/destruction), the peer-close control callback,
                // or on_shutdown() disconnected this session before the read completion arrived.
                break;
            default:
                // Reads start only after a successful handshake; failed attempts never start one.
                assert(false && "Read completions require connected, disconnecting, or disconnected state");
                LERROR("{} Unexpected state: {}", id(), enum_to_string(status_));
                break;
            }
        }

    public:
        // UNCONFIRMED TODO[WS-SEND-REENTRY] (stage WS-03): Try sending from inside the ws_data receiver handler
        // to reproduce re-entry into send.
        bool send(const std::string& text)
        {
            switch (status_)
            {
            case ws_status::connected:
            {
                // Blocking send. Calls and completions must be serialized on one executor;
                // write/ping/close coordination is still a design item.
                beast::error_code ec;
                const std::size_t bytes_sent = stream_.write(net::buffer(text), ec);
                if (ec || bytes_sent != text.size())
                {
                    LERROR("{} Send error: {}", id(), ec ? ec.message() : "incomplete send");
                    shutdown();
                    return false;
                }
                return true;
            }
            default:
                LWARN("{} Can't send data in state: {}", id(), enum_to_string(status_));
                return false;
            }
        }

    private:
        void shutdown()
        {
            switch (status_)
            {
            case ws_status::connected:
            {
                set_status<ws_status::disconnecting>();
                if (receive_watchdog_timer_)
                    receive_watchdog_timer_->cancel();
                if (active_ping_timer_)
                    active_ping_timer_->cancel();

                stream_.async_close(websocket::close_code::normal,
                                    beast::bind_front_handler(&session::on_shutdown, shared_from_this()));
                break;
            }
            default:
                assert(false && "shutdown requires connected state");
                LERROR("{} Unexpected state: {}", id(), enum_to_string(status_));
                break;
            }
        }

        void on_shutdown(beast::error_code ec)
        {
            switch (status_)
            {
            case ws_status::disconnecting:
            {
                // This completion is the first path to finish disconnection.
                set_status<ws_status::disconnected>();
                auto error_message = disconnect_reason(ec);
                if (!error_message.empty())
                {
                    LWARN("{} Shutdown error: {}", id(), error_message);
                }

                delegate_(
                    *parent_,
                    event::on_disconnected{.ts = std::chrono::steady_clock::now(), .reason = std::move(error_message)});
                break;
            }
            case ws_status::disconnected:
                // close() (including reconnect/destruction), peer-close notification,
                // or a read-error completion has already disconnected the session.
                break;
            default:
                assert(false && "Shutdown completions require disconnecting or disconnected state");
                LERROR("{} Unexpected state: {}", id(), enum_to_string(status_));
                break;
            }
        }

        ws_status status_{ws_status::init};
        web_socket* parent_;
        delegate delegate_;
        std::string id_;
        std::unique_ptr<setup_state> setup_;
        websocket::stream<ws_tls_stream> stream_;
        beast::flat_buffer buffer_;
        network_timestamp last_recv_ts_{};
        std::optional<net::steady_timer> receive_watchdog_timer_;
        std::optional<net::steady_timer> active_ping_timer_;
    };

    // DONE TODO[WS-EAGER-SESSION] (stage WS-02): Always create a session so web_socket::send
    // (and others) need not check for a valid session_ pointer.
    // DONE TODO[WS-MOVE-COPY] (stage WS-02): Consider enabling move/copy construction and assignment
    // for web_socket (why are they disabled?).
    // DONE TODO[WS-TLS-OWNERSHIP] (stage WS-02): Review why the TLS context is at the web_socket level
    // (could it move to session?).
    web_socket::web_socket(net::any_io_executor executor, const ::nom::url& address)
        : url_(address), io_(std::make_unique<io_state>(std::move(executor)))
    {
        if ("wss" != url_.scheme())
            throw std::invalid_argument("Wrong web_socket URL scheme (expected wss://): " + url_.scheme());
        io_->ssl_ctx.set_default_verify_paths();
        io_->ssl_ctx.set_verify_mode(ssl::verify_peer);
        session_ = std::make_shared<session>(this);
    }

    web_socket::web_socket(net::any_io_executor executor,
                           std::string_view host,
                           std::uint16_t port,
                           std::string_view target)
        : web_socket(std::move(executor), ::nom::url("wss", host, port, target))
    {
    }
    web_socket::web_socket(web_socket&& other) : web_socket(other.io_->executor, other.url_)
    {
        // Prepare a replacement before touching other, so a failed allocation leaves it unchanged.
        swap_state(other);
    }

    web_socket& web_socket::operator=(web_socket&& other) noexcept
    {
        if (this != &other)
        {
            swap_state(other);
            other.close();
        }
        return *this;
    }

    void web_socket::swap_state(web_socket& other) noexcept
    {
        static_assert(std::is_nothrow_swappable_v<ws_params> && std::is_nothrow_swappable_v<::nom::url>);
        std::swap(params_, other.params_);
        std::swap(url_, other.url_);
        std::swap(io_, other.io_);
        std::swap(session_, other.session_);
        std::swap(session_->parent_, other.session_->parent_);
    }

    web_socket::~web_socket()
    {
        close();
    }

    void web_socket::update_delegate(const delegate& callbacks)
    {
        session_->update_delegate(callbacks);
    }

    const web_socket::delegate& web_socket::callbacks() const noexcept
    {
        return session_->callbacks();
    }

    void web_socket::update_params(const ws_params& params)
    {
        if (const auto result = params.validate(); !result)
            throw std::invalid_argument(result.error());

        switch (status())
        {
        case ws_status::init:
        case ws_status::conn_failed:
        case ws_status::disconnected:
        {
            static_assert(std::is_nothrow_swappable_v<ws_params>);
            auto next = params;
            std::swap(params_, next);
            break;
        }
        default:
            throw std::logic_error("Cannot update parameters while the WebSocket session is active");
        }
    }

    void web_socket::connect(const delegate& callbacks)
    {
        if (ws_status::init == session_->status())
        {
            session_->update_delegate(callbacks);
        }
        else
        {
            close();
            auto next_session = std::make_shared<session>(this);
            next_session->update_delegate(callbacks);
            session_ = std::move(next_session);
        }
        session_->connect();
    }

    void web_socket::close() noexcept
    {
        session_->close();
    }

    bool web_socket::send(const std::string& text)
    {
        return session_->send(text);
    }

    const std::string& web_socket::session_id() const noexcept
    {
        return session_->id();
    }

    ws_status web_socket::status() const noexcept
    {
        return session_->status();
    }

} // namespace nom
