// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0

module;

#include <base/util/enum.h>
#include <boost/asio/ts/netfwd.hpp>

export module nom.asio_comm:https;

import std;
export import :events;
export import nom.core;

export namespace nom
{
    struct http_header
    {
        std::string name;
        std::string value;
    };

    struct http_request
    {
        std::string method{"GET"};
        std::string target{"/"};
        std::vector<http_header> headers{};
        std::string body{};
        bool keep_alive{true};

        [[nodiscard]] std::expected<void, std::string> validate() const;
    };

    struct http_response
    {
        network_timestamp ts{};
        unsigned status_code{};
        std::vector<http_header> headers;
        std::string body;
    };

    struct https_params
    {
        unsigned connect_timeout_sec{30}; // Overall DNS, TCP and TLS budget.
        unsigned request_timeout_sec{30}; // Write through final response; starts on dequeue.
        unsigned shutdown_timeout_sec{5};
        // Body limits are independent of headers and socket buffers. Queue count includes the active request.
        unsigned max_response_body_size{1024 * 1024};
        unsigned max_request_body_size{1024 * 1024};
        unsigned max_header_size{16 * 1024};
        unsigned max_pending_requests{64};

        [[nodiscard]] std::expected<void, std::string> validate() const;
    };

    NOM_ENUM(https_status, std::uint8_t, init, connecting, connected, disconnecting, conn_failed, disconnected)

    // HTTP/1.1 over verified TLS. All operations and callbacks share one serial execution domain.
    class https
    {
        struct io_state;
        struct session;

    public:
        using request_id = std::uint64_t;
        // Exceptions escaping a delegate are fatal; the application boundary must log and exit.
        using https_delegate = ::nom::delegate<std::function<void(https&, const event::on_connected&)>,
                                               std::function<void(https&, const event::on_disconnected&)>,
                                               std::function<void(https&, const event::network_error&)>,
                                               std::function<void(https&, request_id, const http_response&)>>;

        https(boost::asio::any_io_executor executor, const ::nom::url& address);
        https(const https&) = delete;
        https& operator=(const https&) = delete;
        https(https&& other);
        https& operator=(https&& other) noexcept;
        ~https();

        void connect(const https_delegate& delegate);
        // Abort silently; pending request IDs have no response after this call.
        void close() noexcept;
        // Own the request before returning; safe from response callbacks. No automatic retry.
        request_id send(const http_request& request);

        void update_params(const https_params& params);
        [[nodiscard]] const https_params& params() const noexcept
        {
            return params_;
        }
        [[nodiscard]] const ::nom::url& url() const noexcept
        {
            return url_;
        }
        [[nodiscard]] https_status status() const noexcept;
        [[nodiscard]] bool is_connected() const noexcept
        {
            return status() == https_status::connected;
        }
        [[nodiscard]] const std::string& session_id() const noexcept;

    private:
        void swap_state(https& other) noexcept;

        https_params params_;
        ::nom::url url_;
        std::shared_ptr<io_state> io_;
        std::shared_ptr<session> session_;
    };
} // namespace nom
