// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

#include "../../common_app_nodes.h"

import std;
import nom.asio_comm;
import nom.assembly;
import nom.spd_logger;

#include "config.h"

namespace nom::event
{
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
    // One connection, lowercase symbol names, case-sensitive channels, one correlation ID:
    // {"method":"SUBSCRIBE","params":["btcusdt@bookTicker","btcusdt@trade","btcusdt@depth@100ms"],"id":1}
    // https://github.com/binance/binance-spot-api-docs/blob/master/web-socket-streams.md#live-subscribingunsubscribing-to-streams
    // Limits: 1024 active streams/connection; 5 client-to-server messages/s, including JSON commands
    // and control Ping/Pong frames. A multi-stream SUBSCRIBE is one command; leave room for Pongs.
    // https://github.com/binance/binance-spot-api-docs/blob/master/web-socket-streams.md#websocket-limits
    // Check symbol-count * channel-count (341 symbols with the three default channels). Entries are
    // not deduplicated. Send one batch; reject excess streams rather than split across connections.
    // There is no rate limiter; splitting commands would not increase the active-stream allowance.
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

template <typename Traits> struct market_data_logger
{
    void on_response(const nom::event::market_data& event)
    {
        LINFO("{}", event.data);
    }
};

template <typename Traits> class binance_spot_client
{
public:
    explicit binance_spot_client(const app_config& config)
        : request_(subscription_request(config)), client_(nom::ctx(this).io.get_executor(), nom::url(config.endpoint))
    {
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
        // Workflow: connect -> SUBSCRIBE -> {"result":null,"id":1} or error -> continuous data.
        // send() confirms the local write only; this logger forwards replies without validating the ack.
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
        // Illustrative data, selected fields only (prices/quantities are decimal strings):
        // trade: {"e":"trade","s":"BTCUSDT","t":7,"p":"60000","q":"0.01"}
        // bookTicker: {"u":42,"s":"BTCUSDT","b":"60000","B":"1","a":"60001","A":"2"}
        // depth: {"e":"depthUpdate","s":"BTCUSDT","U":43,"u":44,"b":[["60000","0"]],"a":[]}
        // https://github.com/binance/binance-spot-api-docs/blob/master/web-socket-streams.md#trade-streams
        // https://github.com/binance/binance-spot-api-docs/blob/master/web-socket-streams.md#individual-symbol-book-ticker-streams
        // A book consumer must reconcile a snapshot with U/u and apply absolute quantities (zero deletes).
        // https://github.com/binance/binance-spot-api-docs/blob/master/web-socket-streams.md#how-to-manage-a-local-order-book-correctly
        // This capture example forwards all payloads, including acknowledgements/errors, without parsing.
        nom::relay<Traits>::pass_response(
            this, nom::event::market_data{.ts = event.ts, .source = connection.session_id(), .data = event.sv_data()});
    }

    void operator()(nom::web_socket&, const nom::event::on_disconnected& event)
    {
        // While reading, Beast answers server control Pings automatically; no application ping is needed.
        // Binance rotates connections after 24 h. This example exits; a continuous service must reconnect,
        // resubscribe and restore book state. Connection rules are linked beside app_config::endpoint.
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

// Keep the terminator before every other stop handler: pass_request only visits later nodes.
struct binance_spot_assembly
    : nom::assembly<binance_spot_assembly, asio_service, app_terminator, market_data_logger, binance_spot_client>
{
};

struct app : nom::relay<binance_spot_assembly>
{
    explicit app(const app_config& config) : nom::relay<binance_spot_assembly>(config)
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
        std::cout << "Usage: binance_spot_feeder [--symbols BTCUSDT,ETHUSDT]\n"
                     "                           [--channels bookTicker,trade,depth@100ms] [--endpoint wss://HOST/ws]\n"
                     "Defaults: BTCUSDT, all three channels, wss://stream.binance.com:443/ws.\n"
                     "Log all incoming messages until disconnected or stopped with Ctrl+C.\n";
        std::cout << "Optional: --exit-after-sec SECONDS (default: no limit).\n"
                     "0 exits after connection before subscribing; positive values count from connection.\n"
                     "Ctrl+C/SIGTERM and timeout exit successfully; failures exit with status 1.\n";
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
