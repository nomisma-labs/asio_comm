// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

// GCC 16's Ubuntu snapshot needs stdio declarations before module imports at -O2.
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ == 16
#include <cstdio>
#endif

#include <spd_logger/logging.h>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/io_context.hpp>

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
        // Our client probes solicit Pongs for its receive-idle monitor on quiet subscriptions.
        // This is an example policy, separate from Binance's server Ping every 3 min / 10 min Pong
        // allowance (see Connect above). Beast still answers server Pings automatically while reading.
        auto params = client_.params();
        params.active_ping = nom::ws_params::client_ping_probe{};
        params.receive_watchdog = nom::ws_params::receive_data_watchdog{};
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
            client_.connect(nom::web_socket::delegate{std::ref(*this)});
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
        LINFO("Subscribing: {}", request_);
        if (!connection.send(request_))
        {
            LERROR("Subscription send failed");
            on_request(nom::event::service_command::stop);
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
        on_request(nom::event::service_command::stop);
    }

    void operator()(nom::web_socket&, const nom::event::network_error& event)
    {
        LERROR("{}: {}", event.description, event.message);
        on_request(nom::event::service_command::stop);
    }

private:
    std::string request_;
    nom::web_socket client_;
};

struct binance_perp_assembly
    : nom::assembly<binance_perp_assembly, asio_service, market_data_logger, binance_perp_client>
{
};

struct app : nom::relay<binance_perp_assembly>
{
    explicit app(const app_config& config) : nom::relay<binance_perp_assembly>(config)
    {
    }

    void start()
    {
        request(nom::event::service_command::start);
        // Relay calls and all WebSocket callbacks stay on this one io_context thread.
        ctx().io.run();
    }
};

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--help")
    {
        std::cout << "Usage: binance_perp_feeder [--symbols BTCUSDT,ETHUSDT]\n"
                     "                           [--channels bookTicker,trade,depth@100ms] [--endpoint "
                     "wss://HOST/public/ws]\n"
                     "Defaults: BTCUSDT, all three channels, wss://fstream.binance.com/public/ws.\n"
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
        app(config).start();
        nom::flush_logger(nom::main_logger);
        return 1; // An ended connection interrupts this continuous feed.
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
