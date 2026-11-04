// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

// GCC 16's Ubuntu snapshot needs stdio declarations before module imports at -O2.
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ == 16
#include <cstdio>
#endif

#include <spd_logger/logging.h>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/system/error_code.hpp>
#include <csignal>

import std;
import nom.asio_comm;
import nom.assembly;
import nom.spd_logger;

#include "config.h"

namespace nom::event
{
    enum class service_command
    {
        start,
        stop
    };

    enum class feed_event
    {
        connected,
        failed
    };

    // Borrowed views are consumed synchronously by the relay before the WebSocket callback returns.
    struct market_data
    {
        network_timestamp ts;
        std::string_view source;
        std::string_view data;
    };
} // namespace nom::event

std::string subscription_request(const app_config& config)
{
    // Multiplex lowercase symbol/channel pairs; id identifies the reply, not a market-data stream.
    // {"method":"SUBSCRIBE","params":["btcusdt@bookTicker","btcusdt@trade","btcusdt@depth@100ms"],"id":1}
    // /ws uses raw payloads; /stream would add a {"stream":...,"data":...} envelope.
    // https://developers.binance.com/en/docs/products/derivatives-trading-usds-futures/websocket-market-streams/Live-Subscribing-Unsubscribing-to-streams
    // Limits: 1024 active streams/connection and 10 client-to-server messages/s.
    // https://developers.binance.com/en/docs/products/derivatives-trading-usds-futures/websocket-market-streams/Connect
    // Check symbol-count * channel-count (341 symbols with the three default channels), without
    // deduplication. All pairs go in one SUBSCRIBE; larger selections are rejected, not split.
    // There is no rate limiter. For future batches, conservatively reserve room for control traffic;
    // batching cannot bypass the connection's total active-stream limit.
    std::string request = R"({"method":"SUBSCRIBE","params":[)";
    bool first = true;
    for (auto symbol : config.symbols)
    {
        symbol = normalized_symbol(std::move(symbol));
        // Validated symbols and channel names need no JSON escaping.
        for (const auto& channel : config.channels)
        {
            if (!first)
                request += ',';
            first = false;
            request += '"' + symbol + '@' + channel + '"';
        }
    }
    request += R"(],"id":1})";
    return request;
}

template <typename Traits> struct asio_service
{
    struct context_mixin
    {
        // The assembly constructs context services before nodes and destroys them after nodes.
        boost::asio::io_context io;
    };
};

template <typename Traits> struct market_data_logger
{
    void on_response(const nom::event::market_data& event)
    {
        LINFO("{}", event.data);
    }
};

template <typename Traits> class binance_perp_client
{
public:
    explicit binance_perp_client(const app_config& config)
        : request_(subscription_request(config)), client_(nom::ctx(this).io.get_executor(), nom::url(config.endpoint))
    {
        // Client control probes check connection availability even when market data is quiet.
        // This is an example policy, separate from Binance's server Ping every 3 min / 10 min Pong
        // allowance (see Connect above). Beast still answers server Pings automatically while reading.
        auto params = client_.params();
        params.active_ping = nom::ws_params::client_ping_probe{};
        client_.update_params(params);
    }

    void on_request(nom::event::service_command command)
    {
        switch (command)
        {
        case nom::event::service_command::start:
        {
            std::ostringstream address;
            address << client_.url();
            LINFO("Connecting to {}", address.str());
            client_.connect(nom::web_socket::ws_delegate{std::ref(*this)});
            break;
        }
        case nom::event::service_command::stop:
            client_.close();
            break;
        }
    }

    void operator()(nom::web_socket& connection, const nom::event::on_connected&)
    {
        // Workflow: connect -> SUBSCRIBE -> {"result":null,"id":1} or error -> continuous events.
        // send() is local write completion; this example logs the ack without establishing readiness.
        LINFO("Connected: {}", connection.session_id());
        nom::relay<Traits>::pass_response(this, nom::event::feed_event::connected);
        // A response handler may close the connection before subscription.
        if (!connection.is_connected())
            return;
        LINFO("Subscribing: {}", request_);
        if (!connection.send(request_))
        {
            LERROR("Subscription send failed");
            nom::relay<Traits>::pass_response(this, nom::event::feed_event::failed);
        }
    }

    void operator()(nom::web_socket& connection, const nom::event::ws_data& event)
    {
        // Selected fields with illustrative values; raw trade shape follows the observation in README.md:
        // trade: {"e":"trade","s":"BTCUSDT","t":7,"p":"60000","q":"0.01","T":1700000000000}
        // bookTicker: {"e":"bookTicker","s":"BTCUSDT","u":42,"b":"60000","B":"1","a":"60001","A":"2"}
        // depth: {"e":"depthUpdate","s":"BTCUSDT","U":43,"u":44,"pu":42,"b":[["60000","0"]],"a":[]}
        // Documented quote/depth fields:
        // https://developers.binance.com/en/docs/catalog/core-trading-derivatives-trading-usd-s-m-futures/api/ws-streams/public
        // To build a book, reconcile the REST snapshot and check pu against the previous u; quantities
        // replace levels, and zero deletes. This logger neither reconciles nor validates ack/error payloads.
        // https://developers.binance.com/en/docs/products/derivatives-trading-usds-futures/websocket-market-streams/How-to-manage-a-local-order-book-correctly
        nom::relay<Traits>::pass_response(
            this, nom::event::market_data{.ts = event.ts, .source = connection.session_id(), .data = event.sv_data()});
    }

    void operator()(nom::web_socket&, const nom::event::on_disconnected& event)
    {
        // The documented 24 h rotation also reaches here. A continuous service must reconnect,
        // resubscribe and restore snapshots; this example stops so an interrupted capture is visible.
        LWARN("Disconnected: {}", event.reason);
        nom::relay<Traits>::pass_response(this, nom::event::feed_event::failed);
    }

    void operator()(nom::web_socket&, const nom::event::network_error& event)
    {
        LERROR("{}: {}", event.description, event.message);
        nom::relay<Traits>::pass_response(this, nom::event::feed_event::failed);
    }

private:
    std::string request_;
    nom::web_socket client_;
};

template <typename Traits> class app_terminator
{
public:
    struct context_mixin
    {
        // Unset while running; the first stop fixes the process result.
        std::optional<int> exit_code;
    };

    explicit app_terminator(const app_config& config)
        : signals_(nom::ctx(this).io, SIGINT, SIGTERM), exit_after_sec_(config.exit_after_sec)
    {
    }

    void on_request(nom::event::service_command command)
    {
        switch (command)
        {
        case nom::event::service_command::start:
            signals_.async_wait(
                [this](const boost::system::error_code& error, int signal_number)
                {
                    if (!error && !nom::ctx(this).exit_code)
                    {
                        LINFO("Stopping on signal {}", signal_number);
                        stop(0);
                    }
                });
            nom::relay<Traits>::pass_request(this, command);
            break;
        case nom::event::service_command::stop:
            stop(0);
            break;
        }
    }

    void on_response(nom::event::feed_event event)
    {
        switch (event)
        {
        case nom::event::feed_event::connected:
            if (!exit_after_sec_)
                break;
            if (*exit_after_sec_ == 0)
            {
                LINFO("Stopping after connection (--exit-after-sec 0)");
                stop(0);
                break;
            }
            exit_timer_.emplace(nom::ctx(this).io.get_executor());
            exit_timer_->arm_after(std::chrono::seconds{*exit_after_sec_},
                                   [this]
                                   {
                                       LINFO("Stopping after {} seconds connected", *exit_after_sec_);
                                       stop(0);
                                   });
            break;
        case nom::event::feed_event::failed:
            stop(1);
            break;
        }
    }

private:
    void stop(int exit_code)
    {
        // The first stop fixes the result, even if another signal or failure is already queued.
        if (nom::ctx(this).exit_code)
            return;
        nom::ctx(this).exit_code = exit_code;
        if (exit_timer_)
            exit_timer_->cancel();
        signals_.cancel();
        nom::relay<Traits>::pass_request(this, nom::event::service_command::stop);
    }

    boost::asio::signal_set signals_;
    std::optional<unsigned> exit_after_sec_;
    std::optional<nom::timer> exit_timer_;
};

struct binance_perp_assembly
    : nom::assembly<binance_perp_assembly, asio_service, app_terminator, market_data_logger, binance_perp_client>
{
};

struct app : nom::relay<binance_perp_assembly>
{
    explicit app(const app_config& config) : nom::relay<binance_perp_assembly>(config)
    {
    }

    int start()
    {
        request(nom::event::service_command::start);
        // Drain canceled handlers before destroying the nodes; do not stop the context early.
        ctx().io.run();
        return ctx().exit_code.value_or(1);
    }
};

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--help")
    {
        std::cout << "Usage: binance_perp_feeder [--symbols BTCUSDT,ETHUSDT]\n"
                     "                           [--channels bookTicker,trade,depth@100ms] [--endpoint "
                     "wss://HOST/public/ws]\n"
                     "                           [--exit-after-sec SECONDS]\n"
                     "Defaults: BTCUSDT, all three channels, wss://fstream.binance.com/public/ws.\n"
                     "No time limit by default; 0 exits after connection, before subscribing.\n"
                     "Positive limits count seconds after connection.\n"
                     "Log all incoming messages until disconnected or stopped with Ctrl+C.\n";
        return 0;
    }

    try
    {
        const auto config = parse_arguments(argc, argv);
        if (const auto result = config.validate(); !result)
        {
            std::cerr << result.error() << '\n';
            return 1;
        }
        nom::init_logger(nom::main_logger, config.log);
        const auto result = app(config).start();
        nom::flush_logger(nom::main_logger);
        return result;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
