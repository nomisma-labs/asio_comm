// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

// GCC 16's Ubuntu snapshot needs stdio declarations before module imports at -O2.
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ == 16
#include <cstdio>
#endif

#include <spd_logger/logging.h>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/system/error_code.hpp>

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
    // Local aliases map to tickers/trades/books. books gives a 400-level snapshot then 100 ms diffs;
    // it provides public depth without the access requirements of books-l2-tbt.
    // https://www.okx.com/docs-v5/en/#order-book-trading-market-data-ws-order-book-channel
    // trades may combine matches (count identifies aggregation); do not assume one fill per record.
    // https://www.okx.com/docs-v5/en/#order-book-trading-market-data-ws-trades-channel
    // Batch channel/instrument pairs on one connection, e.g. the depth-only request:
    // {"op":"subscribe","args":[{"channel":"books","instId":"BTC-USDT"}]}
    // https://www.okx.com/docs-v5/en/#overview-websocket-subscribe
    // Limits: subscribe/unsubscribe/login share 480 requests/hour per connection; many args in one
    // command consume one request. The separate 3/s/IP limit is for connection attempts, not batches.
    // https://www.okx.com/docs-v5/en/#overview-websocket-connect
    // For many 50/400-level books, OKX recommends fewer than 30 channels per connection (see books
    // docs above). This example does not enforce that recommendation or distribute subscriptions.
    // It sends one batch, rejects oversized requests, and has no hourly budget or rate limiter.
    std::string request = R"({"op":"subscribe","args":[)";
    bool first = true;
    for (const auto& symbol : symbols)
    {
        for (const auto& channel : config.channels)
        {
            if (!first)
                request += ',';
            first = false;
            const auto name = channel == "ticker" ? "tickers" : channel == "trade" ? "trades" : "books";
            request += R"({"channel":")" + std::string(name) + R"(","instId":")" + symbol + R"("})";
        }
    }
    request += "]}";
    // app_config::validate checks the 64 KiB bound, including this JSON envelope.
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

template <typename Traits> class okex_spot_client
{
public:
    explicit okex_spot_client(const app_config& config)
        : requests_(subscription_requests(config)),
          client_(nom::ctx(this).io.get_executor(), nom::url(config.endpoint)), ping_timer_(nom::ctx(this).io)
    {
        auto params = client_.params();
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
            ping_timer_.cancel();
            client_.close();
            break;
        }
    }

    void operator()(nom::web_socket& connection, const nom::event::on_connected&)
    {
        // Connect -> subscribe -> event:"subscribe" per arg (or event:"error") -> data + text heartbeat.
        // send() confirms a local write; this raw logger does not validate subscription acknowledgements.
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
        start_ping_timer();
    }

    void operator()(nom::web_socket& connection, const nom::event::ws_data& event)
    {
        // Selected fields with illustrative values; arg identifies the channel/instrument:
        // {"arg":{"channel":"tickers","instId":"BTC-USDT"},"data":[{"last":"60000","bidPx":"59999"}]}
        // {"arg":{"channel":"trades","instId":"BTC-USDT"},"data":[{"px":"60000","sz":"0.01","count":"1"}]}
        // {"arg":{"channel":"books","instId":"BTC-USDT"},"action":"snapshot",
        //   "data":[{"seqId":10,"prevSeqId":-1,"bids":[["60000","1","0","2"]],"asks":[]}]}
        // https://www.okx.com/docs-v5/en/#order-book-trading-market-data-ws-tickers-channel
        // books later uses action:"update". A book consumer must apply levels and follow seqId/prevSeqId
        // continuity rules (including resets); this example just relays payloads, acks, errors and pong.
        // Full depth tuples and sequence rules: the order-book link beside subscription_requests().
        nom::relay<Traits>::pass_response(
            this, nom::event::market_data{.ts = event.ts, .source = connection.session_id(), .data = event.sv_data()});
    }

    void operator()(nom::web_socket&, const nom::event::on_disconnected& event)
    {
        // Stop also cancels the application heartbeat. A reconnecting consumer must resubscribe and
        // establish a new snapshot; this capture example exits instead of hiding a gap in received data.
        LWARN("Disconnected: {}", event.reason);
        on_request(nom::event::service_command::stop);
    }

    void operator()(nom::web_socket&, const nom::event::network_error& event)
    {
        LERROR("{}: {}", event.description, event.message);
        on_request(nom::event::service_command::stop);
    }

private:
    // OKX prescribes text ping after receive silence of N < 30 s, then checking for pong within N.
    // This example uses a simpler fixed 20 s schedule and the client's receive-idle monitor; it does
    // not match each pong to a deadline. Control Ping/Pong cannot replace these application messages.
    // https://www.okx.com/docs-v5/en/#overview-websocket-connect
    void start_ping_timer()
    {
        ping_timer_.expires_after(std::chrono::seconds(20));
        ping_timer_.async_wait(
            [this](const boost::system::error_code& error)
            {
                if (error || !client_.is_connected())
                    return;
                if (!client_.send("ping"))
                {
                    LERROR("Heartbeat send failed");
                    on_request(nom::event::service_command::stop);
                    return;
                }
                start_ping_timer();
            });
    }

    std::vector<std::string> requests_;
    nom::web_socket client_;
    boost::asio::steady_timer ping_timer_;
};

struct okex_spot_assembly : nom::assembly<okex_spot_assembly, asio_service, market_data_logger, okex_spot_client>
{
};

struct app : nom::relay<okex_spot_assembly>
{
    explicit app(const app_config& config) : nom::relay<okex_spot_assembly>(config)
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
        std::cout << "Usage: okex_spot_feeder [--symbols BTC-USDT,ETH-USDT]\n"
                     "                           [--channels ticker,trade,depth] [--endpoint wss://HOST/ws]\n"
                     "Defaults: BTC-USDT, all three channels, wss://ws.okx.com/ws/v5/public.\n"
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
