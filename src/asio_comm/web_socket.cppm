// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0

module;

#include <base/util/enum.h>

#include <boost/asio/ts/netfwd.hpp>

export module nom.asio_comm:web_socket;

import std;
export import :events;
export import :ws_params;
export import nom.core;

export namespace nom
{
    NOM_ENUM(ws_status, std::uint8_t, init, connecting, connected, disconnecting, conn_failed, disconnected)

    // Detection paths are implemented; live/network-fault reproductions remain pending.
    // DEV-DONE/LIVE-TEST-PENDING TODO[WS-CABLE-LOSS] (stage WS-06):
    // Reproduce hard disconnection (unplug the network cable).
    // DEV-DONE/LIVE-TEST-PENDING TODO[WS-INTERFACE-LOSS] (stage WS-06): Reproduce Docker-based interface disconnection
    // (silent loss requires a control-Ping policy or subscriber data-freshness timeout).
    // DEV-DONE/LIVE-TEST-PENDING TODO[WS-PEER-CLOSE] (stage WS-06): Reproduce normal server disconnection
    // (e.g. Binance resets the MD connection once per day).
    // TODO[WS-EXCEPTIONS] (stage WS-08): Consider making the interface noexcept for everything
    // except the constructor.

    class web_socket
    {
        struct io_state;
        struct session;

    public:
        using ws_delegate = ::nom::delegate<std::function<void(web_socket& ws, const event::ws_data&)>,
                                            std::function<void(web_socket& ws, const event::on_connected&)>,
                                            std::function<void(web_socket& ws, const event::on_disconnected&)>,
                                            std::function<void(web_socket& ws, const event::network_error&)>>;

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

        void update_delegate(const ws_delegate& delegate);
        [[nodiscard]] const ws_delegate& delegate() const noexcept;

        void update_params(const ws_params& params);
        [[nodiscard]] const ws_params& params() const noexcept
        {
            return params_;
        }

        void connect(const ws_delegate& delegate);
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
