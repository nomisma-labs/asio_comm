// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

module;

#include <spd_logger/logging.h>

#include <boost/asio/ssl/host_name_verification.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/url/parse.hpp>
#include <cassert>

module nom.asio_comm;

import std;
import nom.spd_logger;
import :http_validation;

namespace nom
{
    std::expected<void, std::string> http_request::validate() const
    {
        constexpr std::array methods{"GET", "HEAD", "POST", "PUT", "DELETE", "PATCH", "OPTIONS"};
        if (std::ranges::find(methods, method) == methods.end())
            return std::unexpected("/method must be GET, HEAD, POST, PUT, DELETE, PATCH or OPTIONS");
        if (const auto parsed = boost::urls::parse_origin_form(target); !parsed)
            return std::unexpected("/target must be an encoded absolute path with an optional query and no fragment "
                                   "(e.g. /orders?symbol=BTC%2FUSD); Boost.URL: " +
                                   parsed.error().message());
        for (std::size_t i = 0; i < headers.size(); ++i)
        {
            const auto& header = headers[i];
            const auto path = "/headers/" + std::to_string(i);
            if (!detail::is_http_header_name(header.name))
                return std::unexpected(path + "/name must be a nonempty HTTP token: ASCII letters, digits or "
                                              "!#$%&'*+-.^_`|~");
            if (!detail::is_http_header_value(header.value))
                return std::unexpected(path + "/value contains a control character; only horizontal tab is allowed "
                                              "(CR, LF and NUL are forbidden)");
            switch (boost::beast::http::string_to_field(header.name))
            {
            case boost::beast::http::field::host:
            case boost::beast::http::field::content_length:
            case boost::beast::http::field::transfer_encoding:
            case boost::beast::http::field::connection:
            case boost::beast::http::field::upgrade:
            case boost::beast::http::field::expect:
            case boost::beast::http::field::trailer:
                return std::unexpected(path + "/name is reserved for HTTPS framing/control");
            default:
                break;
            }
        }
        return {};
    }

    std::expected<void, std::string> https_params::validate() const
    {
        if (!connect_timeout_sec || !request_timeout_sec || !shutdown_timeout_sec)
            return std::unexpected("HTTPS timeouts must be positive");
        if (!max_response_body_size || !max_request_body_size || !max_header_size || !max_pending_requests)
            return std::unexpected("HTTPS size and queue limits must be positive");
        return {};
    }

    struct https::io_state
    {
        explicit io_state(boost::asio::any_io_executor ex) : executor(std::move(ex))
        {
            ssl_ctx.set_default_verify_paths();
            ssl_ctx.set_verify_mode(boost::asio::ssl::verify_peer);
        }
        boost::asio::any_io_executor executor;
        boost::asio::ssl::context ssl_ctx{boost::asio::ssl::context::tls_client};
    };

    struct https::session : std::enable_shared_from_this<session>
    {
        using error_code = boost::system::error_code;
        using tcp = boost::asio::ip::tcp;
        using wire_request = boost::beast::http::request<boost::beast::http::string_body>;
        using parser = boost::beast::http::response_parser<boost::beast::http::string_body>;
        struct pending_request
        {
            request_id id;
            wire_request message;
        };

        session(https& owner, const https_delegate& delegate)
            : owner_(&owner), io_(owner.io_), address_(owner.url_), params_(owner.params_), delegate_(delegate),
              resolver_(io_->executor), stream_(io_->executor, io_->ssl_ctx), deadline_(io_->executor)
        {
            std::ostringstream id;
            id << "HTTPS[" << address_ << ':' << next_session_id.fetch_add(1, std::memory_order_relaxed) << ']';
            id_ = id.str();
            host_header_ =
                address_.host().find(':') == std::string::npos ? address_.host() : '[' + address_.host() + ']';
            if (address_.port() != 443)
                host_header_ += ':' + std::to_string(address_.port());
        }

        void close() noexcept
        {
            switch (status_)
            {
            case https_status::init:
                return;
            case https_status::connecting:
                status_ = https_status::conn_failed;
                break;
            case https_status::connected:
            case https_status::disconnecting:
                status_ = https_status::disconnected;
                break;
            case https_status::conn_failed:
            case https_status::disconnected:
                // Preserve the outcome, but retry cleanup if an earlier attempt failed.
                break;
            }
            deadline_.cancel();
            // Cleanup is best effort. Each failure must leave the remaining cleanup reachable.
            try
            {
                // Asio cancellation replaces a shared token and can fail to allocate it.
                resolver_.cancel();
            }
            catch (...)
            {
            }
            try
            {
                error_code ignored;
                boost::beast::get_lowest_layer(stream_).socket().close(ignored);
            }
            catch (...)
            {
            }
            queue_.clear();
            // active_request_ and response_ back asynchronous operations; retain them until their handlers drain.
        }

        void connect()
        {
            switch (status_)
            {
            case https_status::init:
            {
                status_ = https_status::connecting;
                // SNI is for DNS names; host verification also handles numeric IP addresses.
                error_code address_error;
                boost::asio::ip::make_address(address_.host(), address_error);
                if (address_error && !SSL_set_tlsext_host_name(stream_.native_handle(), address_.host().c_str()))
                    throw boost::system::system_error(
                        error_code(static_cast<int>(::ERR_get_error()), boost::asio::error::get_ssl_category()));
                stream_.set_verify_callback(boost::asio::ssl::host_name_verification(address_.host()));
                deadline_.arm_after(std::chrono::seconds{params_.connect_timeout_sec},
                                    [weak = weak_from_this()]
                                    {
                                        if (auto self = weak.lock())
                                            self->on_connection_deadline();
                                    });
                resolver_.async_resolve(address_.host(),
                                        std::to_string(address_.port()),
                                        boost::beast::bind_front_handler(&session::on_resolve, shared_from_this()));
                break;
            }
            default:
                assert(false && "HTTPS connect requires init state");
                LERROR("{} Unexpected connect state: {}", id(), enum_to_string(status_));
                break;
            }
        }

        void on_connection_deadline()
        {
            switch (status_)
            {
            case https_status::connecting:
                close();
                delegate_(*owner_,
                          event::network_error{std::chrono::steady_clock::now(), "HTTPS connection", "timeout"});
                break;
            case https_status::conn_failed:
                break;
            default:
                assert(false && "HTTPS connection deadline requires connecting state");
                LERROR("{} Unexpected connection deadline state: {}", id(), enum_to_string(status_));
                break;
            }
        }

        void on_resolve(error_code error, tcp::resolver::results_type endpoints)
        {
            switch (status_)
            {
            case https_status::connecting:
                if (error)
                {
                    close();
                    delegate_(*owner_,
                              event::network_error{std::chrono::steady_clock::now(), "Resolve", error.message()});
                    break;
                }
                boost::beast::get_lowest_layer(stream_).async_connect(
                    endpoints, boost::beast::bind_front_handler(&session::on_tcp_connect, shared_from_this()));
                break;
            case https_status::conn_failed:
                // close() or a deadline abandoned this connection attempt.
                break;
            default:
                assert(false && "HTTPS resolution requires connecting or conn_failed state");
                LERROR("{} Unexpected resolve completion state: {}", id(), enum_to_string(status_));
                break;
            }
        }

        void on_tcp_connect(error_code error, const tcp::endpoint&)
        {
            switch (status_)
            {
            case https_status::connecting:
                if (error)
                {
                    close();
                    delegate_(*owner_,
                              event::network_error{std::chrono::steady_clock::now(), "TCP connect", error.message()});
                    break;
                }
                stream_.async_handshake(boost::asio::ssl::stream_base::client,
                                        boost::beast::bind_front_handler(&session::on_handshake, shared_from_this()));
                break;
            case https_status::conn_failed:
                break;
            default:
                assert(false && "HTTPS TCP connect completion requires connecting or conn_failed state");
                LERROR("{} Unexpected TCP connect completion state: {}", id(), enum_to_string(status_));
                break;
            }
        }

        void on_handshake(error_code error)
        {
            switch (status_)
            {
            case https_status::connecting:
                if (error)
                {
                    close();
                    delegate_(*owner_,
                              event::network_error{std::chrono::steady_clock::now(), "TLS handshake", error.message()});
                    break;
                }
                deadline_.cancel();
                status_ = https_status::connected;
                delegate_(*owner_, event::on_connected{std::chrono::steady_clock::now()});
                break;
            case https_status::conn_failed:
                break;
            default:
                assert(false && "HTTPS TLS handshake completion requires connecting or conn_failed state");
                LERROR("{} Unexpected TLS handshake completion state: {}", id(), enum_to_string(status_));
                break;
            }
        }

        // TODO: review this logic and implement sync send - this is important for sending orders without queueing
        request_id send(const http_request& request)
        {
            switch (status_)
            {
            case https_status::connected:
            {
                if (const auto valid = request.validate(); !valid)
                    throw std::invalid_argument(valid.error());
                if (queue_.size() + static_cast<unsigned>(bool(active_request_)) >= params_.max_pending_requests)
                    throw std::length_error("HTTPS request queue is full");
                if (request.body.size() > params_.max_request_body_size)
                    throw std::length_error("HTTPS request body exceeds max_request_body_size");
                if (next_request_id_ == std::numeric_limits<request_id>::max())
                    throw std::overflow_error("HTTPS request IDs exhausted");

                // Build before accepting: validation/allocation failure leaves the existing queue untouched.
                auto pending = std::make_shared<pending_request>();
                auto& message = pending->message;
                message.version(11);
                message.method_string(request.method);
                message.target(request.target);
                message.set(boost::beast::http::field::host, host_header_);
                for (const auto& header : request.headers)
                    message.insert(header.name, header.value);
                if (message.find(boost::beast::http::field::user_agent) == message.end())
                    message.set(boost::beast::http::field::user_agent, "nomisma/asio_comm");
                message.body() = request.body;
                message.keep_alive(request.keep_alive);
                message.prepare_payload();
                std::size_t header_size = request.method.size() + request.target.size() + 14;
                for (const auto& field : message)
                    header_size += field.name_string().size() + field.value().size() + 4;
                if (header_size > params_.max_header_size)
                    throw std::length_error("HTTPS request headers exceed max_header_size");
                pending->id = next_request_id_ + 1;
                queue_.push_back(pending);
                next_request_id_ = pending->id;
                start_next_request();
                return pending->id;
            }
            default:
                throw std::logic_error("HTTPS send requires a connected session");
            }
        }

        // TODO: reflect async sending workflow in https.md state machine
        // TODO: Introduce request pipelining: send requests sequentially without waiting for earlier responses,
        // and match responses to outstanding requests in send order.
        // TODO: Introduce request batching: serialize multiple complete HTTP requests into one combined write
        // operation; each request remains a separate HTTP message with its own response.
        void start_next_request()
        {
            assert(status_ == https_status::connected);
            if (active_request_ || notifying_response_ || queue_.empty())
                return;
            active_request_ = std::move(queue_.front());
            queue_.pop_front();
            deadline_.arm_after(std::chrono::seconds{params_.request_timeout_sec},
                                [weak = weak_from_this()]
                                {
                                    if (auto self = weak.lock())
                                        self->on_request_deadline();
                                });
            boost::beast::http::async_write(
                stream_,
                active_request_->message,
                boost::beast::bind_front_handler(&session::on_request_written, shared_from_this()));
        }

        void on_request_deadline()
        {
            switch (status_)
            {
            case https_status::connected:
                assert(active_request_);
                close();
                delegate_(*owner_,
                          event::on_disconnected{std::chrono::steady_clock::now(),
                                                 "HTTPS request " + std::to_string(active_request_->id) + ": timeout"});
                break;
            case https_status::disconnected:
                break;
            default:
                assert(false && "HTTPS request deadline requires connected state");
                LERROR("{} Unexpected request deadline state: {}", id(), enum_to_string(status_));
                break;
            }
        }

        void on_request_written(error_code error, std::size_t)
        {
            switch (status_)
            {
            case https_status::connected:
                if (error)
                {
                    close();
                    delegate_(
                        *owner_,
                        event::on_disconnected{std::chrono::steady_clock::now(), "HTTP write: " + error.message()});
                    break;
                }
                read_response();
                break;
            case https_status::disconnected:
                // close() or a request deadline abandoned the write.
                break;
            default:
                assert(false && "HTTPS write completion requires connected or disconnected state");
                LERROR("{} Unexpected HTTP write completion state: {}", id(), enum_to_string(status_));
                break;
            }
        }

        void read_response()
        {
            assert(status_ == https_status::connected && active_request_);
            response_.emplace();
            response_->body_limit(params_.max_response_body_size);
            response_->header_limit(params_.max_header_size);
            response_->skip(active_request_->message.method() == boost::beast::http::verb::head);
            boost::beast::http::async_read(stream_,
                                           buffer_,
                                           *response_,
                                           boost::beast::bind_front_handler(&session::on_response, shared_from_this()));
        }

        void on_response(error_code error, std::size_t)
        {
            switch (status_)
            {
            case https_status::connected:
            {
                if (error)
                {
                    close();
                    delegate_(
                        *owner_,
                        event::on_disconnected{std::chrono::steady_clock::now(), "HTTP read: " + error.message()});
                    break;
                }
                const auto code = response_->get().result_int();
                if (code == 101)
                {
                    close();
                    delegate_(*owner_,
                              event::on_disconnected{std::chrono::steady_clock::now(),
                                                     "HTTP response: Protocol switching is not supported"});
                    break;
                }
                if (code < 200)
                {
                    // Informational responses share the current request's ID and deadline; only deliver its final
                    // response.
                    read_response();
                    break;
                }

                http_response result;
                const auto id = active_request_->id;
                const bool reusable = active_request_->message.keep_alive() && response_->get().keep_alive();
                auto message = response_->release();
                result.ts = std::chrono::steady_clock::now();
                result.status_code = message.result_int();
                result.body = std::move(message.body());
                for (const auto& field : message)
                    result.headers.push_back({std::string(field.name_string()), std::string(field.value())});
                deadline_.cancel();
                response_.reset();
                active_request_.reset();
                if (!reusable)
                {
                    status_ = https_status::disconnecting;
                    // Accepted but unanswered requests are covered by on_disconnected; never retry them.
                    queue_.clear();
                }
                notifying_response_ = true;
                delegate_(*owner_, id, result);
                notifying_response_ = false;
                // Any over-read bytes remain in buffer_ for the next response, including after a 1xx response.
                // The delegate may have closed, replaced, moved or destroyed the owner.
                if (status_ == https_status::connected)
                    start_next_request();
                else if (status_ == https_status::disconnecting)
                    start_tls_shutdown();
                break;
            }
            case https_status::disconnected:
                // close() or a request deadline abandoned the read.
                break;
            default:
                assert(false && "HTTPS response completion requires connected or disconnected state");
                LERROR("{} Unexpected HTTP response completion state: {}", id(), enum_to_string(status_));
                break;
            }
        }

        void start_tls_shutdown()
        {
            assert(status_ == https_status::disconnecting);
            assert(!active_request_ && !notifying_response_ && queue_.empty());
            deadline_.arm_after(std::chrono::seconds{params_.shutdown_timeout_sec},
                                [weak = weak_from_this()]
                                {
                                    if (auto self = weak.lock())
                                        self->on_shutdown_deadline();
                                });
            stream_.async_shutdown(boost::beast::bind_front_handler(&session::on_tls_shutdown, shared_from_this()));
        }

        void on_shutdown_deadline()
        {
            switch (status_)
            {
            case https_status::disconnecting:
                close();
                delegate_(*owner_, event::on_disconnected{std::chrono::steady_clock::now(), "HTTPS shutdown: timeout"});
                break;
            case https_status::disconnected:
                break;
            default:
                assert(false && "HTTPS shutdown deadline requires disconnecting state");
                LERROR("{} Unexpected shutdown deadline state: {}", id(), enum_to_string(status_));
                break;
            }
        }

        void on_tls_shutdown(error_code error)
        {
            switch (status_)
            {
            case https_status::disconnecting:
                // No response is being parsed here. Truncation during a response is never accepted.
                if (error == boost::asio::error::eof || error == boost::asio::ssl::error::stream_truncated)
                    error = {};
                close();
                delegate_(*owner_,
                          event::on_disconnected{std::chrono::steady_clock::now(),
                                                 error ? "TLS shutdown: " + error.message() : "HTTPS closed"});
                break;
            case https_status::disconnected:
                // close() or the shutdown deadline already ended the session.
                break;
            default:
                assert(false && "HTTPS TLS shutdown completion requires disconnecting or disconnected state");
                LERROR("{} Unexpected TLS shutdown completion state: {}", id(), enum_to_string(status_));
                break;
            }
        }

        https_status status() const noexcept
        {
            return status_;
        }

        const std::string& id() const noexcept
        {
            return id_;
        }

    private:
        friend void https::swap_state(https& other) noexcept;

        inline static std::atomic<std::uint64_t> next_session_id{};
        https* owner_;
        std::shared_ptr<io_state> io_;
        ::nom::url address_;
        https_params params_;
        https_delegate delegate_;
        https_status status_{https_status::init};
        std::string id_;
        std::string host_header_;
        tcp::resolver resolver_;
        boost::beast::ssl_stream<boost::beast::tcp_stream> stream_;
        // One timer serves the mutually exclusive connection, request and shutdown deadlines.
        timer deadline_;
        boost::beast::flat_buffer buffer_;
        std::optional<parser> response_;
        std::deque<std::shared_ptr<pending_request>> queue_;
        std::shared_ptr<pending_request> active_request_;
        request_id next_request_id_{};
        bool notifying_response_{};
    };

    https::https(boost::asio::any_io_executor executor, const ::nom::url& address) : url_(address)
    {
        if (address.scheme() != "https")
            throw std::invalid_argument("HTTPS requires an https:// URL");
        io_ = std::make_shared<io_state>(std::move(executor));
        session_ = std::make_shared<session>(*this, https_delegate{});
    }

    https::https(https&& other) : https(other.io_->executor, other.url_)
    {
        // Prepare a replacement before touching other, so a failed allocation leaves it unchanged.
        swap_state(other);
    }

    https& https::operator=(https&& other) noexcept
    {
        if (this != &other)
        {
            swap_state(other);
            other.close();
        }
        return *this;
    }

    void https::swap_state(https& other) noexcept
    {
        static_assert(std::is_nothrow_swappable_v<https_params> && std::is_nothrow_swappable_v<::nom::url>);
        std::swap(params_, other.params_);
        std::swap(url_, other.url_);
        std::swap(io_, other.io_);
        std::swap(session_, other.session_);
        std::swap(session_->owner_, other.session_->owner_);
    }

    https::~https()
    {
        close();
    }

    void https::connect(const https_delegate& delegate)
    {
        // Allocate before replacing, so a failed copy leaves the existing connection intact.
        auto replacement = std::make_shared<session>(*this, delegate);
        close();
        session_ = replacement;
        replacement->connect();
    }

    void https::close() noexcept
    {
        session_->close();
    }
    https::request_id https::send(const http_request& request)
    {
        auto current = session_;
        return current->send(request);
    }
    https_status https::status() const noexcept
    {
        return session_->status();
    }
    const std::string& https::session_id() const noexcept
    {
        return session_->id();
    }

    void https::update_params(const https_params& params)
    {
        if (const auto valid = params.validate(); !valid)
            throw std::invalid_argument(valid.error());
        switch (status())
        {
        case https_status::connecting:
        case https_status::connected:
        case https_status::disconnecting:
            throw std::logic_error("HTTPS parameters may only change before connect or after close");
        default:
            params_ = params;
            break;
        }
    }
} // namespace nom
