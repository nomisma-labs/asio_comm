// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0

module;

#include <spd_logger/logging.h>

#include <boost/asio/compose.hpp>
#include <boost/asio/ssl/host_name_verification.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <cassert>
#include <fmt/ostream.h>

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

        struct connection_setup
        {
            explicit connection_setup(const net::any_io_executor& executor) : resolver(executor), deadline(executor)
            {
            }

            tcp::resolver resolver;
            nom::timer deadline;
            std::string host_header;
        };

        struct active_ping
        {
            enum ap_status
            {
                init,
                waiting_to_ping,
                sending_ping,
                send_done,
                failed
            };
            explicit active_ping(const net::any_io_executor& executor) : timer(executor)
            {
            }

            nom::timer timer;
            ap_status status{init};
            std::uint64_t sequence{};
            std::string payload;
        };

        template <ws_status NewStatus> void set_status() noexcept
        {
            static_assert(NewStatus != ws_status::init, "Only construction may select init");
            assert(static_cast<std::uint8_t>(status_) < static_cast<std::uint8_t>(NewStatus) &&
                   "State transitions must change the state in increasing order");
            status_ = NewStatus;
            if constexpr (NewStatus == ws_status::connected || NewStatus == ws_status::conn_failed)
                connection_setup_.reset();
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

        void update_delegate(const ws_delegate& delegate)
        {
            delegate_ = delegate;
        }

        const ws_delegate& delegate() const noexcept
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

            // Cleanup is best effort. Keep each attempt independent, without logging or ws_delegate calls.
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
            if (active_ping_)
                active_ping_->timer.cancel();
            if (passive_ping_timer_)
                passive_ping_timer_->cancel();
        }

        ws_status status() const noexcept
        {
            return status_;
        }

        // TODO IDs and stage definitions: ws_current_work.md.
        // DEV-DONE/LIVE-TEST-PENDING TODO[WS-CONNECT-DEADLINE] (stage WS-06): Make the connection procedure time-bound
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
                    LINFO("{} connecting with params {}", id(), fmt::streamed(parent_->params_));
                    if (parent_->params_.active_ping)
                        active_ping_.emplace(parent_->io_->executor);
                    if (parent_->params_.passive_ping)
                        passive_ping_timer_.emplace(parent_->io_->executor);
                    connection_setup_ = std::make_unique<connection_setup>(parent_->io_->executor);
                    connection_setup_->deadline.arm_after(
                        std::chrono::seconds{parent_->params_.async_operation_timeout_sec},
                        [weak = weak_from_this()]
                        {
                            if (auto self = weak.lock())
                                self->on_connection_deadline();
                        });
                    connection_setup_->resolver.async_resolve(
                        parent_->url_.host(),
                        std::to_string(parent_->url_.port()),
                        beast::bind_front_handler(&session::on_resolve, shared_from_this()));
                }
                catch (...)
                {
                    close();
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
        void on_connection_deadline()
        {
            switch (status_)
            {
            case ws_status::connecting:
            {
                const auto now = std::chrono::steady_clock::now();
                auto message = std::format("Connection deadline exceeded after {} seconds",
                                           parent_->params_.async_operation_timeout_sec);
                close();
                LWARN("{} {}", id(), message);
                delegate_(*parent_,
                          event::network_error{
                              .ts = now, .description = "Connection timeout", .message = std::move(message)});
                break;
            }
            case ws_status::conn_failed:
                break;
            default:
                assert(false && "Connection deadline requires connecting state");
                LERROR("{} Unexpected connection deadline state: {}", id(), enum_to_string(status_));
                break;
            }
        }

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
                    close();
                    LERROR("{} Resolve error: {}", id(), ec.message());
                    notify_error("Resolve error", ec.message());
                    break;
                }

                try
                {
                    beast::get_lowest_layer(stream_).async_connect(
                        endpoints, beast::bind_front_handler(&session::on_connect, shared_from_this()));
                }
                catch (const std::exception& error)
                {
                    close();
                    notify_error("TCP connect setup error", error.what());
                }
                catch (...)
                {
                    close();
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
                    close();
                    LERROR("{} Connect error: {}", id(), ec.message());
                    notify_error("Connect error", ec.message());
                    break;
                }

                if (!SSL_set_tlsext_host_name(stream_.next_layer().native_handle(), parent_->url_.host().c_str()))
                {
                    ec = beast::error_code(static_cast<int>(::ERR_get_error()), net::error::get_ssl_category());
                    close();
                    LERROR("{} Connect error: {}", id(), ec.message());
                    notify_error("Connect/SSL error", ec.message());
                    break;
                }

                try
                {
                    apply_socket_options();

                    // The HTTP Host header brackets IPv6 literals; DNS and TLS use the unbracketed host.
                    const auto& host = parent_->url_.host();
                    connection_setup_->host_header = (std::string::npos == host.find(':') ? host : '[' + host + ']') +
                                                     ':' + std::to_string(ep.port());
                    stream_.next_layer().set_verify_callback(ssl::host_name_verification(host));
                    stream_.next_layer().async_handshake(
                        ssl::stream_base::client,
                        beast::bind_front_handler(&session::on_ssl_handshake, shared_from_this()));
                }
                catch (const std::exception& error)
                {
                    close();
                    notify_error("TLS handshake setup error", error.what());
                }
                catch (...)
                {
                    close();
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
                    close();
                    LERROR("{} Connect/SSL handshake error: {}", id(), ec.message());
                    notify_error("Connect/SSL handshake error", ec.message());
                    break;
                }

                try
                {
                    // The setup deadline covers the whole attempt, including the WebSocket upgrade.
                    stream_.set_option(
                        websocket::stream_base::timeout{.handshake_timeout = websocket::stream_base::none(),
                                                        .idle_timeout = websocket::stream_base::none(),
                                                        .keep_alive_pings = false});
                    stream_.read_message_max(parent_->params_.max_message_size);
                    stream_.set_option(websocket::stream_base::decorator(
                        [this](websocket::request_type& req)
                        {
                            assert(ws_status::connecting == status_);
                            req.set(http::field::user_agent, parent_->params_.agent_name);
                            for (const auto& header : parent_->params_.headers)
                                req.set(header.name, header.value);
                        }));
                    stream_.async_handshake(connection_setup_->host_header,
                                            parent_->url_.target(),
                                            beast::bind_front_handler(&session::on_handshake, shared_from_this()));
                }
                catch (const std::exception& error)
                {
                    close();
                    notify_error("WebSocket handshake setup error", error.what());
                }
                catch (...)
                {
                    close();
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
                if (ec)
                {
                    close();
                    LERROR("{} Connect/Handshake error: {}", id(), ec.message());
                    notify_error("Connect/Handshake error", ec.message());
                    break;
                }
                try
                {
                    // Preserve Beast's closing-handshake timeout; idle timeout and automatic Pings stay disabled.
                    stream_.set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));
                }
                catch (const std::exception& error)
                {
                    close();
                    notify_error("WebSocket handshake completion error", error.what());
                    break;
                }
                catch (...)
                {
                    close();
                    notify_error("WebSocket handshake completion error", "Unknown exception");
                    break;
                }
                const auto connected_at = std::chrono::steady_clock::now();
                LDEBUG("{} connected", id());
                set_status<ws_status::connected>();
                delegate_(*parent_, event::on_connected{.ts = connected_at});
                // The ws_delegate may call close() or trigger a write-failure shutdown before receive/timer work
                // starts.
                if (ws_status::connected != status_)
                    break;

                if (active_ping_)
                    start_active_ping_timer();

                // The stream owns this callback; pending operations keep the session alive.
                stream_.control_callback(beast::bind_front_handler(&session::on_control, this));

                if (passive_ping_timer_)
                    start_passive_ping_timer(connected_at);
                stream_.async_read(buffer_, beast::bind_front_handler(&session::on_receive, shared_from_this()));
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

        void on_control(websocket::frame_type kind, std::string_view payload)
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
                    if (ws_status::connected == status_ && passive_ping_timer_)
                        start_passive_ping_timer(std::chrono::steady_clock::now());
                    break;
                }
                case websocket::frame_type::pong:
                {
                    if (active_ping_)
                        on_active_pong(payload);
                    break;
                }
                case websocket::frame_type::close:
                {
                    LDEBUG("{} Close request received from server", id());
                    if (active_ping_)
                        active_ping_->timer.cancel();
                    if (passive_ping_timer_)
                        passive_ping_timer_->cancel();
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
                assert(false && "Control handler requires connected, disconnecting, or disconnected state");
                LERROR("{} Unexpected state: {}", id(), enum_to_string(status_));
                break;
            }
        }

        void start_passive_ping_timer(network_timestamp received_at)
        {
            assert(passive_ping_timer_ && parent_->params_.passive_ping);
            // The first deadline uses handshake time; later deadlines use server Ping receipt time.
            passive_ping_timer_->arm_at(received_at +
                                            std::chrono::seconds{parent_->params_.passive_ping->ping_timeout_sec},
                                        [weak = weak_from_this()]
                                        {
                                            if (auto self = weak.lock())
                                                self->on_passive_ping_timer();
                                        });
        }

        void on_passive_ping_timer()
        {
            switch (status_)
            {
            case ws_status::connected:
            {
                assert(passive_ping_timer_ && parent_->params_.passive_ping);
                const auto now = std::chrono::steady_clock::now();
                auto reason = std::format("Passive Ping timeout: no server Ping within {} seconds",
                                          parent_->params_.passive_ping->ping_timeout_sec);
                // An unresponsive peer must not delay disconnection through a close handshake.
                close();
                LWARN("{} {}", id(), reason);
                delegate_(*parent_, event::on_disconnected{.ts = now, .reason = std::move(reason)});
                break;
            }
            case ws_status::disconnecting:
            case ws_status::disconnected:
                // Canceled waits and queued expiries cannot rearm or notify after shutdown/close.
                break;
            default:
                assert(false && "Passive Ping waits require connected, disconnecting, or disconnected state");
                LERROR("{} Unexpected state: {}", id(), enum_to_string(status_));
                break;
            }
        }

        void start_active_ping_timer()
        {
            assert(active_ping_ && parent_->params_.active_ping);
            active_ping_->status = active_ping::waiting_to_ping;
            active_ping_->timer.arm_after(std::chrono::seconds{parent_->params_.active_ping->ping_period_sec},
                                          [weak = weak_from_this()]
                                          {
                                              if (auto self = weak.lock())
                                                  self->on_active_ping_timer();
                                          });
        }

        void fail_active_ping(std::string reason)
        {
            assert(active_ping_);
            if (active_ping_->status == active_ping::failed)
                return;
            assert(ws_status::connected == status_);
            active_ping_->status = active_ping::failed;
            const auto now = std::chrono::steady_clock::now();
            close();
            LWARN("{} {}", id(), reason);
            delegate_(*parent_, event::on_disconnected{.ts = now, .reason = std::move(reason)});
        }

        void on_active_ping_timer()
        {
            // Pending operations can outlive the connection and its owner.
            if (ws_status::connected != status_)
                return;
            assert(active_ping_ && parent_->params_.active_ping);
            switch (active_ping_->status)
            {
            case active_ping::waiting_to_ping:
                active_ping_->payload = std::to_string(++active_ping_->sequence);
                active_ping_->status = active_ping::sending_ping;
                active_ping_->timer.arm_after(std::chrono::seconds{parent_->params_.async_operation_timeout_sec},
                                              [weak = weak_from_this()]
                                              {
                                                  if (auto self = weak.lock())
                                                      self->on_active_ping_expire_timer();
                                              });
                stream_.async_ping(websocket::ping_data{active_ping_->payload.data(), active_ping_->payload.size()},
                                   beast::bind_front_handler(&session::on_active_ping, shared_from_this()));
                break;
            case active_ping::failed:
                break;
            case active_ping::init:
            case active_ping::sending_ping:
            case active_ping::send_done:
            default:
            {
                auto reason = std::format("Active Ping interval timer: unexpected status {} (expected waiting_to_ping)",
                                          std::to_underlying(active_ping_->status));
                LERROR("{} {}", id(), reason);
                assert(false && "Active Ping interval completion requires waiting_to_ping status");
                fail_active_ping(std::move(reason));
                break;
            }
            }
        }

        void on_active_ping_expire_timer()
        {
            if (ws_status::connected != status_)
                return;
            assert(active_ping_ && parent_->params_.active_ping);
            switch (active_ping_->status)
            {
            case active_ping::sending_ping:
                fail_active_ping(std::format("Active Ping send timeout: write did not complete within {} seconds",
                                             parent_->params_.async_operation_timeout_sec));
                break;
            case active_ping::failed:
                break;
            case active_ping::init:
            case active_ping::waiting_to_ping:
            case active_ping::send_done:
            default:
            {
                auto reason = std::format("Active Ping send timer: unexpected status {} (expected sending_ping)",
                                          std::to_underlying(active_ping_->status));
                LERROR("{} {}", id(), reason);
                assert(false && "Active Ping send deadline requires sending_ping status");
                fail_active_ping(std::move(reason));
                break;
            }
            }
        }

        void on_active_pong_timer()
        {
            if (ws_status::connected != status_)
                return;
            assert(active_ping_ && parent_->params_.active_ping);
            switch (active_ping_->status)
            {
            case active_ping::send_done:
                fail_active_ping(std::format("Active Pong timeout: no Pong within {} seconds",
                                             parent_->params_.active_ping->pong_timeout_sec));
                break;
            case active_ping::failed:
                break;
            case active_ping::init:
            case active_ping::waiting_to_ping:
            case active_ping::sending_ping:
            default:
            {
                auto reason = std::format("Active Pong timer: unexpected status {} (expected send_done)",
                                          std::to_underlying(active_ping_->status));
                LERROR("{} {}", id(), reason);
                assert(false && "Active Pong timer completion requires send_done status");
                fail_active_ping(std::move(reason));
                break;
            }
            }
        }

        void on_active_ping(beast::error_code ec)
        {
            if (ws_status::connected != status_)
                return;
            if (net::error::operation_aborted == ec)
                return;
            if (ec)
            {
                fail_active_ping("Active Ping send error: " + ec.message());
                return;
            }

            assert(active_ping_ && parent_->params_.active_ping);
            switch (active_ping_->status)
            {
            case active_ping::sending_ping:
                LDEBUG("{} sent ping to server", id());
                active_ping_->status = active_ping::send_done;
                active_ping_->timer.arm_after(std::chrono::seconds{parent_->params_.active_ping->pong_timeout_sec},
                                              [weak = weak_from_this()]
                                              {
                                                  if (auto self = weak.lock())
                                                      self->on_active_pong_timer();
                                              });
                break;
            case active_ping::failed:
                break;
            case active_ping::init:
            case active_ping::waiting_to_ping:
            case active_ping::send_done:
            default:
                fail_active_ping(std::format("Active Ping write completed in unexpected status {}",
                                             std::to_underlying(active_ping_->status)));
                break;
            }
        }

        void on_active_pong(std::string_view payload)
        {
            if (ws_status::connected != status_)
                return;
            assert(active_ping_ && parent_->params_.active_ping);
            switch (active_ping_->status)
            {
            case active_ping::send_done:
                if (active_ping_->payload != payload)
                {
                    LWARN("{} Active Pong payload mismatch: expected {:?}, received {:?}",
                          id(),
                          active_ping_->payload,
                          payload);
                }
                start_active_ping_timer();
                break;
            case active_ping::failed:
                break;
            case active_ping::init:
            case active_ping::waiting_to_ping:
            case active_ping::sending_ping:
            default:
                fail_active_ping(std::format("Active Pong received in unexpected status {}",
                                             std::to_underlying(active_ping_->status)));
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
            // shutdown() after a write failure ran before this read completion.
            // A previous event::ws_data callback can also trigger shutdown(), then rearm the read below.
            case ws_status::disconnecting:
            {
                if (ec)
                {
                    if (active_ping_)
                        active_ping_->timer.cancel();
                    if (passive_ping_timer_)
                        passive_ping_timer_->cancel();
                    set_status<ws_status::disconnected>();
                    delegate_(*parent_,
                              event::on_disconnected{.ts = std::chrono::steady_clock::now(),
                                                     .reason = disconnect_reason(ec)});
                    break;
                }

                const auto mutable_buffer = buffer_.data(); // Non-owning view; no payload copy.

                delegate_(*parent_,
                          event::ws_data{.ts = std::chrono::steady_clock::now(),
                                         .data = std::span<const char>(static_cast<char*>(mutable_buffer.data()),
                                                                       mutable_buffer.size())});
                buffer_.consume(buffer_.size());

                // The ws_delegate may close or replace this session; do not rearm after disconnection.
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
                if (active_ping_)
                    active_ping_->timer.cancel();
                if (passive_ping_timer_)
                    passive_ping_timer_->cancel();

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
        ws_delegate delegate_;
        std::string id_;
        std::unique_ptr<connection_setup> connection_setup_;
        websocket::stream<ws_tls_stream> stream_;
        beast::flat_buffer buffer_;
        std::optional<active_ping> active_ping_;
        std::optional<nom::timer> passive_ping_timer_;
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

    void web_socket::update_delegate(const ws_delegate& delegate)
    {
        session_->update_delegate(delegate);
    }

    const web_socket::ws_delegate& web_socket::delegate() const noexcept
    {
        return session_->delegate();
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

    void web_socket::connect(const ws_delegate& delegate)
    {
        if (ws_status::init == session_->status())
        {
            session_->update_delegate(delegate);
        }
        else
        {
            close();
            auto next_session = std::make_shared<session>(this);
            next_session->update_delegate(delegate);
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
