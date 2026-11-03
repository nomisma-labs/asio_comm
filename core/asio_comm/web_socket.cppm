// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

module;

#include <base/util/enum.h>

#include <boost/asio/ts/netfwd.hpp>

export module nom.asio_comm;

import std;
export import :ws_params;
export import nom.url;
export import nom.assembly;

export namespace nom
{
    class web_socket;
}

namespace nom
{
    namespace detail
    {
        template <typename... Events> struct ws_delegate
        {
            using type = delegate<std::function<void(web_socket& ws, Events)>...>;
        };
    } // namespace detail
} // namespace nom

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

    NOM_ENUM(ws_status, std::uint8_t, init, connecting, connected, disconnecting, conn_failed, disconnected)

    // Detection paths are implemented; live/network-fault reproductions remain pending.
    // DEV-DONE/LIVE-TEST-PENDING TODO[WS-CABLE-LOSS] (stage WS-06):
    // Reproduce hard disconnection (unplug the network cable).
    // DEV-DONE/LIVE-TEST-PENDING TODO[WS-INTERFACE-LOSS] (stage WS-06): Reproduce Docker-based interface disconnection
    // (a soft disconnection of a virtual interface may only be caught by the receive watchdog timer).
    // DEV-DONE/LIVE-TEST-PENDING TODO[WS-PEER-CLOSE] (stage WS-06): Reproduce normal server disconnection
    // (e.g. Binance resets the MD connection once per day).
    // TODO[WS-EXCEPTIONS] (stage WS-08): Consider making the interface noexcept for everything
    // except the constructor.

    class web_socket
    {
        struct io_state;
        struct session;

    public:
        using delegate = ::nom::detail::ws_delegate<const event::ws_data&,
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
