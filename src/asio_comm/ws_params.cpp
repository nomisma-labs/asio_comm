// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0

module;

#include <boost/beast/core/string.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/fields.hpp>

module nom.asio_comm;

import std;
import :http_validation;

namespace nom
{
    namespace beast = boost::beast;
    namespace http = beast::http;

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

    std::expected<void, std::string> ws_params::validate() const
    {
        if (async_operation_timeout_sec == 0)
            return std::unexpected("/async_operation_timeout_sec must be positive");
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
        if (!detail::is_http_header_value(agent_name))
            return std::unexpected("/agent_name contains a control character; only horizontal tab is allowed "
                                   "(CR, LF and NUL are forbidden)");

        for (std::size_t i = 0; i < headers.size(); ++i)
        {
            const auto& header = headers[i];
            const auto path = "/headers/" + std::to_string(i);
            if (!detail::is_http_header_name(header.name))
                return std::unexpected(path + "/name must be a nonempty HTTP token: ASCII letters, digits or "
                                              "!#$%&'*+-.^_`|~");
            if (header.name.size() > http::fields::max_name_size)
                return std::unexpected(path + "/name exceeds the supported HTTP field length");
            if (is_handshake_header(header.name))
                return std::unexpected(path + "/name is reserved for the WebSocket handshake");
            if (header.value.size() > http::fields::max_value_size)
                return std::unexpected(path + "/value exceeds the supported HTTP field length");
            if (!detail::is_http_header_value(header.value))
                return std::unexpected(path + "/value contains a control character; only horizontal tab is allowed "
                                              "(CR, LF and NUL are forbidden)");
            for (std::size_t j = 0; j < i; ++j)
                if (beast::iequals(header.name, headers[j].name))
                    return std::unexpected(path + "/name duplicates an earlier header");
        }
        return {};
    }

    std::ostream& operator<<(std::ostream& os, const ws_params& params)
    {
        os << "{agent_name=" << std::format("{:?}", params.agent_name) << ", header_names=[";
        for (std::size_t i = 0; i < params.headers.size(); ++i)
        {
            if (i != 0)
                os << ", ";
            os << std::format("{:?}", params.headers[i].name);
        }
        os << "], socket_send_buf_size=" << params.socket_send_buf_size
           << ", socket_recv_buf_size=" << params.socket_recv_buf_size
           << ", max_message_size=" << params.max_message_size
           << ", async_operation_timeout_sec=" << params.async_operation_timeout_sec << ", passive_ping=";
        if (params.passive_ping)
            os << "{ping_timeout_sec=" << params.passive_ping->ping_timeout_sec << '}';
        else
            os << "disabled";
        os << ", active_ping=";
        if (params.active_ping)
            os << "{ping_period_sec=" << params.active_ping->ping_period_sec
               << ", pong_timeout_sec=" << params.active_ping->pong_timeout_sec << '}';
        else
            os << "disabled";
        return os << '}';
    }
} // namespace nom
