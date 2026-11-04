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

std::vector<std::string> subscription_requests(const app_config& config)
{
    auto symbols = config.symbols;
    for (auto& symbol : symbols)
        symbol = normalized_symbol(std::move(symbol));
    // Deribit documents up to 500 channels per subscription message, not per connection.
    // This example sends one message and rejects larger selections instead of splitting them into batches.
    // https://docs.deribit.com/articles/market-data-collection-best-practices#batch-your-subscription-requests
    // Count symbol/channel pairs: three default channels allow 166 symbols in this single batch.
    // The published public/subscribe budget is about 3.3 requests/s sustained with a 10-request burst;
    // it has a separate limit from ordinary API methods. This example has no pacing/rate limiter.
    // Future batches must respect both the 500-channel message bound and the subscription budget.
    // https://docs.deribit.com/articles/rate-limits
    // Batch channels in one JSON-RPC call; id correlates its acknowledgement. Depth-only example:
    // {"jsonrpc":"2.0","id":1,"method":"public/subscribe","params":{"channels":["book.BTC-PERPETUAL.100ms"]}}
    // https://docs.deribit.com/api-reference/subscription-management/public-subscribe
    // 100ms selects public batched updates without credentials; raw requires authentication.
    // ticker/trade/depth map to ticker/trades/book, respectively; trades carries a batch of records.
    // https://docs.deribit.com/subscriptions/market-data/tickerinstrument_nameinterval
    // https://docs.deribit.com/subscriptions/trades/tradesinstrument_nameinterval
    // https://docs.deribit.com/subscriptions/orderbook/bookinstrument_nameinterval
    std::string request = R"({"jsonrpc":"2.0","id":1,"method":"public/subscribe","params":{"channels":[)";
    bool first = true;
    for (const auto& symbol : symbols)
    {
        for (const auto& channel : config.channels)
        {
            if (!first)
                request += ',';
            first = false;
            const auto name = channel == "ticker" ? "ticker" : channel == "trade" ? "trades" : "book";
            request += '"' + std::string(name) + '.' + symbol + ".100ms\"";
        }
    }
    request += "]}}";
    return {request};
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

template <typename Traits> class deribit_perp_client
{
public:
    explicit deribit_perp_client(const app_config& config)
        : requests_(subscription_requests(config)), client_(nom::ctx(this).io.get_executor(), nom::url(config.endpoint))
    {
        // Control probes check connection availability on quiet feeds. We do not enable
        // public/set_heartbeat: that separate application protocol requires public/test replies
        // to test_request notifications, which control Pongs cannot satisfy.
        // https://docs.deribit.com/api-reference/session-management/public-set_heartbeat
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
        // Connect -> public/subscribe -> id:1 result:[accepted channels] or error -> notifications.
        // send() only completes the local write; this logger does not verify the accepted channel list.
        LINFO("Connected: {}", connection.session_id());
        for (const auto& request : requests_)
        {
            LINFO("Subscribing: {}", request);
            if (!connection.send(request))
            {
                LERROR("Subscription send failed");
                on_request(nom::event::service_command::stop);
                return;
            }
        }
    }

    void operator()(nom::web_socket& connection, const nom::event::ws_data& event)
    {
        // Subscription envelope (selected fields, illustrative values):
        // {"jsonrpc":"2.0","method":"subscription","params":{"channel":"book.BTC-PERPETUAL.100ms",
        //   "data":{"type":"snapshot","change_id":10,"bids":[["new",60000,20]],"asks":[]}}}
        // https://docs.deribit.com/articles/notifications
        // Later book data has type:"change", prev_change_id and [new/change/delete,price,amount] tuples.
        // Consumers must match prev_change_id to the last change_id and recover a snapshot after gaps.
        // ticker data is an object, e.g. {"instrument_name":"BTC-PERPETUAL","best_bid_price":60000}.
        // trades data is an array, e.g. [{"trade_id":"7","price":60000,"amount":20,"direction":"buy"}].
        // Channel schemas are linked beside subscription_requests(); amount units depend on the contract.
        // This raw logger also forwards RPC replies/errors and does not reconstruct or validate a book.
        nom::relay<Traits>::pass_response(
            this, nom::event::market_data{.ts = event.ts, .source = connection.session_id(), .data = event.sv_data()});
    }

    void operator()(nom::web_socket&, const nom::event::on_disconnected& event)
    {
        // Subscriptions belong to this connection. A continuous consumer would reconnect, confirm its
        // subscriptions and rebuild from the new snapshot; this example reports the interruption and exits.
        LWARN("Disconnected: {}", event.reason);
        on_request(nom::event::service_command::stop);
    }

    void operator()(nom::web_socket&, const nom::event::network_error& event)
    {
        LERROR("{}: {}", event.description, event.message);
        on_request(nom::event::service_command::stop);
    }

private:
    std::vector<std::string> requests_;
    nom::web_socket client_;
};

struct deribit_perp_assembly
    : nom::assembly<deribit_perp_assembly, asio_service, market_data_logger, deribit_perp_client>
{
};

struct app : nom::relay<deribit_perp_assembly>
{
    explicit app(const app_config& config) : nom::relay<deribit_perp_assembly>(config)
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
        std::cout << "Usage: deribit_perp_feeder [--symbols BTC-PERPETUAL,ETH-PERPETUAL]\n"
                     "                           [--channels ticker,trade,depth] [--endpoint wss://HOST/ws]\n"
                     "Defaults: BTC-PERPETUAL, all three channels, wss://www.deribit.com/ws/api/v2.\n"
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
