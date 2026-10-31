// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

module;

#include <spd_logger/logging.h>

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
        inline static const std::string null_id = "WS[NONE]";

    private:
        inline static std::atomic<std::uint64_t> next_id{0};

    public:
        session(web_socket* owner)
            : parent_(owner),
              id_{[this]()
                  {
                      assert(parent_);
                      std::ostringstream strm;
                      strm << "WS[" << parent_->url_ << ':' << next_id.fetch_add(1, std::memory_order_relaxed) << ']';
                      return strm.str();
                  }()},
              resolver_(parent_->io_->executor), stream_(parent_->io_->executor, parent_->io_->ssl_ctx),
              idle_timer_(parent_->io_->executor), ping_timer_(parent_->io_->executor)
        {
        }

        const std::string& id() const noexcept
        {
            return id_;
        }

        // A session may outlive its owner while canceled operations complete.
        void close() noexcept
        {
            // Detach first: queued completions cannot reach the old owner or a replacement session.
            parent_ = nullptr;
            status_ = ws_status::disconnected;

            // Cleanup is best effort. Keep each attempt independent, without logging or delegate calls.
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
                resolver_.cancel();
            }
            catch (...)
            {
            }
            try
            {
                idle_timer_.cancel();
            }
            catch (...)
            {
            }
            try
            {
                ping_timer_.cancel();
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
        // TODO[WS-SETUP-STORAGE] (stage WS-09): Dispose of state required only for connection setup
        // after the connection is established (e.g. resolver).
        // TODO[WS-TRANSPORT-OPTIONS] (stage WS-09): Test various connection options
        // (different TLS levels, no SSL?).
        void connect()
        {
            switch (status_)
            {
            case ws_status::disconnected:
            {
                // The public connect() constructs a fresh session before calling this function.
                status_ = ws_status::connecting;

                try
                {
                    resolver_.async_resolve(parent_->url_.host(),
                                            std::to_string(parent_->url_.port()),
                                            beast::bind_front_handler(&session::on_resolve, shared_from_this()));
                }
                catch (...)
                {
                    status_ = ws_status::failed;
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
                // connect() entered connecting and started DNS resolution.
                if (ec)
                {
                    status_ = ws_status::failed;
                    LERROR("{} Resolve error: {}", id(), ec.message());
                    parent_->delegate_(*parent_,
                                       event::network_error{.ts = std::chrono::steady_clock::now(),
                                                            .description = "Resolve error",
                                                            .message = ec.message()});
                    break;
                }

                std::string setup_error;
                try
                {
                    beast::get_lowest_layer(stream_).expires_after(
                        std::chrono::seconds(parent_->params_.async_operation_timeout_sec));
                    beast::get_lowest_layer(stream_).async_connect(
                        endpoints, beast::bind_front_handler(&session::on_connect, shared_from_this()));
                }
                catch (const std::exception& error)
                {
                    status_ = ws_status::failed;
                    setup_error = error.what();
                }
                catch (...)
                {
                    status_ = ws_status::failed;
                    setup_error = "Unknown exception";
                }
                if (ws_status::failed == status_)
                {
                    parent_->delegate_(*parent_,
                                       event::network_error{.ts = std::chrono::steady_clock::now(),
                                                            .description = "TCP connect setup error",
                                                            .message = std::move(setup_error)});
                }
                break;
            }
            case ws_status::disconnected:
                // close() (including reconnect/destruction) abandoned this resolution before completion.
                break;
            default:
                assert(false && "Resolve completions require connecting or disconnected state");
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
                // on_resolve() started TCP connect while the connection attempt remained active.
                if (ec)
                {
                    status_ = ws_status::failed;
                    LERROR("{} Connect error: {}", id(), ec.message());
                    parent_->delegate_(*parent_,
                                       event::network_error{.ts = std::chrono::steady_clock::now(),
                                                            .description = "Connect error",
                                                            .message = ec.message()});
                    break;
                }

                if (!SSL_set_tlsext_host_name(stream_.next_layer().native_handle(), parent_->url_.host().c_str()))
                {
                    status_ = ws_status::failed;
                    ec = beast::error_code(static_cast<int>(::ERR_get_error()), net::error::get_ssl_category());
                    LERROR("{} Connect error: {}", id(), ec.message());
                    parent_->delegate_(*parent_,
                                       event::network_error{.ts = std::chrono::steady_clock::now(),
                                                            .description = "Connect/SSL error",
                                                            .message = ec.message()});
                    break;
                }

                std::string setup_error;
                try
                {
                    apply_socket_options();

                    // The HTTP Host header brackets IPv6 literals; DNS and TLS use the unbracketed host.
                    const auto& host = parent_->url_.host();
                    host_header_ = (std::string::npos == host.find(':') ? host : '[' + host + ']') + ':' +
                                   std::to_string(ep.port());
                    stream_.next_layer().set_verify_callback(ssl::host_name_verification(host));
                    stream_.next_layer().async_handshake(
                        ssl::stream_base::client,
                        beast::bind_front_handler(&session::on_ssl_handshake, shared_from_this()));
                }
                catch (const std::exception& error)
                {
                    status_ = ws_status::failed;
                    setup_error = error.what();
                }
                catch (...)
                {
                    status_ = ws_status::failed;
                    setup_error = "Unknown exception";
                }
                if (ws_status::failed == status_)
                {
                    parent_->delegate_(*parent_,
                                       event::network_error{.ts = std::chrono::steady_clock::now(),
                                                            .description = "TLS handshake setup error",
                                                            .message = std::move(setup_error)});
                }
                break;
            }
            case ws_status::disconnected:
                // close() (including reconnect/destruction) abandoned TCP connect before completion.
                break;
            default:
                assert(false && "TCP connect completions require connecting or disconnected state");
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
                // on_connect() completed TCP/SNI setup and started the TLS handshake.
                if (ec)
                {
                    status_ = ws_status::failed;
                    LERROR("{} Connect/SSL handshake error: {}", id(), ec.message());
                    parent_->delegate_(*parent_,
                                       event::network_error{.ts = std::chrono::steady_clock::now(),
                                                            .description = "Connect/SSL handshake error",
                                                            .message = ec.message()});
                    break;
                }

                std::string setup_error;
                try
                {
                    // Beast's WebSocket timeout takes over after the TLS handshake.
                    beast::get_lowest_layer(stream_).expires_never();
                    stream_.set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));
                    stream_.set_option(websocket::stream_base::decorator(
                        [this](websocket::request_type& req)
                        {
                            assert(ws_status::connecting == status_);
                            req.set(http::field::user_agent, parent_->params_.agent_name);
                            for (const auto& header : parent_->params_.headers)
                                req.set(header.name, header.value);
                        }));
                    stream_.async_handshake(host_header_,
                                            parent_->url_.target(),
                                            beast::bind_front_handler(&session::on_handshake, shared_from_this()));
                }
                catch (const std::exception& error)
                {
                    status_ = ws_status::failed;
                    setup_error = error.what();
                }
                catch (...)
                {
                    status_ = ws_status::failed;
                    setup_error = "Unknown exception";
                }
                if (ws_status::failed == status_)
                {
                    parent_->delegate_(*parent_,
                                       event::network_error{.ts = std::chrono::steady_clock::now(),
                                                            .description = "WebSocket handshake setup error",
                                                            .message = std::move(setup_error)});
                }
                break;
            }
            case ws_status::disconnected:
                // close() (including reconnect/destruction) abandoned the TLS handshake before completion.
                break;
            default:
                assert(false && "TLS handshake completions require connecting or disconnected state");
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
                // on_ssl_handshake() completed TLS setup and started the WebSocket handshake.
                std::string setup_error;
                try
                {
                    beast::get_lowest_layer(stream_).expires_never();
                }
                catch (const std::exception& error)
                {
                    status_ = ws_status::failed;
                    setup_error = error.what();
                }
                catch (...)
                {
                    status_ = ws_status::failed;
                    setup_error = "Unknown exception";
                }
                if (ws_status::failed == status_)
                {
                    parent_->delegate_(*parent_,
                                       event::network_error{.ts = std::chrono::steady_clock::now(),
                                                            .description = "WebSocket handshake completion error",
                                                            .message = std::move(setup_error)});
                    break;
                }
                if (ec)
                {
                    status_ = ws_status::failed;
                    LERROR("{} Connect/Handshake error: {}", id(), ec.message());
                    parent_->delegate_(*parent_,
                                       event::network_error{.ts = std::chrono::steady_clock::now(),
                                                            .description = "Connect/Handshake error",
                                                            .message = ec.message()});
                    break;
                }
                last_recv_ts_ = std::chrono::steady_clock::now();
                LDEBUG("{} connected", id());
                status_ = ws_status::connected;
                parent_->delegate_(*parent_, event::on_connected{.ts = last_recv_ts_});
                // The delegate may call close() or trigger a write-failure shutdown before receive/timer work starts.
                if (ws_status::connected != status_)
                    break;

                if (parent_->params_.enable_outbound_ping)
                {
                    LDEBUG("{} Enable outbound ping", id());
                    start_ping_timer();
                }

                // The stream owns this callback; pending operations keep the session alive.
                stream_.control_callback(beast::bind_front_handler(&session::on_control, this));

                stream_.async_read(buffer_, beast::bind_front_handler(&session::on_receive, shared_from_this()));
                start_idle_timer();
                break;
            }
            case ws_status::disconnected:
                // close() (including reconnect/destruction) abandoned the WebSocket handshake before completion.
                break;
            default:
                assert(false && "WebSocket handshake completions require connecting or disconnected state");
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
                    if (parent_->params_.enable_outbound_ping)
                        last_recv_ts_ = std::chrono::steady_clock::now();
                    LDEBUG("{} Pong response received from server", id());
                    break;
                }
                case websocket::frame_type::close:
                {
                    LDEBUG("{} Close request received from server", id());
                    idle_timer_.cancel();
                    status_ = ws_status::disconnected;
                    parent_->delegate_(
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

        void start_idle_timer()
        {
            const auto ping_interval_sec = parent_->params_.ping_interval_sec;
            const auto idle_timer_grace_period_sec = ping_interval_sec / 2;
            idle_timer_.expires_after(std::chrono::seconds{ping_interval_sec} +
                                      std::chrono::seconds{idle_timer_grace_period_sec});
            idle_timer_.async_wait(beast::bind_front_handler(&session::on_idle_timer, shared_from_this()));
        }

        void start_ping_timer()
        {
            ping_timer_.expires_after(std::chrono::seconds{parent_->params_.ping_interval_sec});
            ping_timer_.async_wait(beast::bind_front_handler(&session::on_ping_timer, shared_from_this()));
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

        void on_idle_timer(beast::error_code ec)
        {
            switch (status_)
            {
            case ws_status::connected:
            {
                // on_handshake() started the idle timer; subsequent healthy expiries rearm it.
                if (ec)
                {
                    if (net::error::operation_aborted == ec)
                        break;

                    LWARN("{} Idle timer error: {}", id(), ec.message());
                    shutdown();
                    break;
                }

                const auto ping_interval_sec = parent_->params_.ping_interval_sec;
                const auto ping_jitter_period_sec = ping_interval_sec / 5; // Pings may arrive later than expected.

                const auto diff_ts =
                    std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - last_recv_ts_)
                        .count();

                if (const bool is_idle_connection =
                        std::chrono::seconds{diff_ts} >
                        std::chrono::seconds{ping_interval_sec} + std::chrono::seconds{ping_jitter_period_sec};
                    is_idle_connection)
                {
                    LWARN("{} Idle timer expired: no tracked receive activity for {} seconds", id(), diff_ts);
                    shutdown();
                    break;
                }

                start_idle_timer();
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
                // The idle timer starts only after a successful handshake; sessions are never reused.
                assert(false && "Idle timer completions require connected, disconnecting, or disconnected state");
                LERROR("{} Unexpected state: {}", id(), enum_to_string(status_));
                break;
            }
        }

        void on_receive(beast::error_code ec, [[maybe_unused]] std::size_t bytes_transferred)
        {
            switch (status_)
            {
            // on_handshake() set connected and started the read loop; no close has started since.
            case ws_status::connected:
            // shutdown() (idle timer or write failure) ran before this read completion.
            // A previous event::ws_data callback can also trigger shutdown(), then rearm the read below.
            case ws_status::disconnecting:
            {
                if (ec)
                {
                    idle_timer_.cancel();
                    status_ = ws_status::disconnected;
                    parent_->delegate_(
                        *parent_,
                        event::on_disconnected{.ts = std::chrono::steady_clock::now(), .reason = ec.message()});
                    break;
                }

                const auto mutable_buffer = buffer_.data(); // Non-owning view; no payload copy.
                last_recv_ts_ = std::chrono::steady_clock::now();

                parent_->delegate_(
                    *parent_,
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
        // TODO[WS-SEND-REENTRY] (stage WS-07): Try sending from inside the ws_data receiver handler
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
                status_ = ws_status::disconnecting;
                idle_timer_.cancel();
                ping_timer_.cancel();

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
                std::string error_message;
                if (ec)
                {
                    error_message = ec.message();
                    LWARN("{} Shutdown error: {}", id(), error_message);
                }

                status_ = ws_status::disconnected;
                parent_->delegate_(
                    *parent_, event::on_disconnected{.ts = std::chrono::steady_clock::now(), .reason = error_message});
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

        ws_status status_{ws_status::disconnected};
        web_socket* parent_; // Null once web_socket::close() has disowned this session.
        std::string id_;
        tcp::resolver resolver_;
        websocket::stream<beast::ssl_stream<beast::tcp_stream>> stream_;
        std::string host_header_;
        beast::flat_buffer buffer_;
        network_timestamp last_recv_ts_{};
        net::steady_timer idle_timer_;
        net::steady_timer ping_timer_;
    };

    // TODO[WS-EAGER-SESSION] (stage WS-02): Always create a session so web_socket::send
    // (and others) need not check for a valid session_ pointer.
    // TODO[WS-MOVE-COPY] (stage WS-02): Consider enabling move/copy construction and assignment
    // for web_socket (why are they disabled?).
    // TODO[WS-TLS-OWNERSHIP] (stage WS-02): Review why the TLS context is at the web_socket level
    // (could it move to session?).
    web_socket::web_socket(net::any_io_executor executor, const ::nom::url& address)
        : io_(std::make_unique<io_state>(std::move(executor))), url_(address)
    {
        if ("wss" != url_.scheme())
            throw std::invalid_argument("Wrong web_socket URL scheme (expected wss://): " + url_.scheme());
        io_->ssl_ctx.set_default_verify_paths();
        io_->ssl_ctx.set_verify_mode(ssl::verify_peer);
    }

    web_socket::web_socket(net::any_io_executor executor,
                           std::string_view host,
                           std::uint16_t port,
                           std::string_view target)
        : web_socket(std::move(executor), ::nom::url("wss", host, port, target))
    {
    }

    web_socket::~web_socket()
    {
        close();
    }

    void web_socket::connect(const delegate& callbacks)
    {
        close();
        delegate_ = callbacks;
        session_ = std::make_shared<session>(this);
        session_->connect();
    }

    void web_socket::close() noexcept
    {
        if (auto active = std::exchange(session_, {}))
            active->close();
    }

    bool web_socket::send(const std::string& text)
    {
        if (session_)
            return session_->send(text);
        return false;
    }

    const std::string& web_socket::session_id() const noexcept
    {
        if (session_)
            return session_->id();
        return session::null_id;
    }

    ws_status web_socket::status() const noexcept
    {
        return session_ ? session_->status() : ws_status::disconnected;
    }

} // namespace nom
