// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0

#include "../../common_app_nodes.h"

import std;
import nom.asio_comm;
import nom.core;

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

std::vector<std::string> subscription_requests(const app_config& config)
{
    auto symbols = config.symbols;
    for (auto& symbol : symbols)
        symbol = normalized_symbol(std::move(symbol));
    // subscribe takes one channel; assign an ID per request while keeping every channel on one socket.
    // {"jsonrpc":"2.0","method":"subscribe","params":{"channel":"lightning_executions_BTC_JPY"},"id":1}
    // https://bf-lightning-api.readme.io/docs/endpoint-json-rpc
    // JSON-RPC batch arrays are supported, with requests processed in array order; this example
    // instead sends individual commands immediately. Defaults use four requests per symbol:
    // ticker, executions, board_snapshot and board. No count, request-size or rate limit is checked.
    // The linked Realtime docs do not specify numeric subscription quotas (reviewed 2026-11-02);
    // this is not an unlimited allowance, and HTTP quotas do not establish a WebSocket budget.
    // https://bf-lightning-api.readme.io/docs/realtime-api
    std::vector<std::string> requests;
    const auto add_channel = [&](const std::string& channel)
    {
        requests.push_back(R"({"jsonrpc":"2.0","method":"subscribe","params":{"channel":")" + channel + R"("},"id":)" +
                           std::to_string(requests.size() + 1) + '}');
    };
    for (const auto& symbol : symbols)
    {
        for (const auto& channel : config.channels)
        {
            if (channel == "ticker")
                add_channel("lightning_ticker_" + symbol);
            else if (channel == "trade")
                add_channel("lightning_executions_" + symbol);
            else
            {
                // Depth needs the snapshot and incremental board channels. Snapshot delivery can be
                // throttled; board updates carry changed levels (size is absolute; zero removes a level).
                // This example captures both without attempting snapshot/delta reconciliation.
                // https://bf-lightning-api.readme.io/docs/realtime-board-snapshot
                // https://bf-lightning-api.readme.io/docs/realtime-board
                add_channel("lightning_board_snapshot_" + symbol);
                add_channel("lightning_board_" + symbol);
            }
        }
    }
    return requests;
}

template <typename Traits> struct market_data_logger
{
    struct context_mixin
    {
        // Count relayed application payloads, which may include acknowledgements and heartbeats.
        std::uint64_t received_messages{};
        std::uint64_t received_payload_bytes{};
    };

    void on_response(const nom::event::market_data& event)
    {
        ++nom::ctx(this).received_messages;
        nom::ctx(this).received_payload_bytes += event.data.size();
        LINFO("{}", event.data);
    }
};

template <typename Traits> class bitflyer_spot_client
{
public:
    explicit bitflyer_spot_client(const app_config& config)
        : requests_(subscription_requests(config)), client_(nom::ctx(this).io.get_executor(), nom::url(config.endpoint))
    {
        // Client control probes are our quiet-feed policy, not a documented exchange-specific interval.
        // Keep reads active for server control frames; no application JSON ping is prescribed here.
        // https://bf-lightning-api.readme.io/docs/realtime-api
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
        // Connect -> subscribe for each channel -> {"jsonrpc":"2.0","id":N,"result":true} or error
        // -> channelMessage notifications. send() is local completion; this logger does not check RPC acks.
        LINFO("Connected: {}", connection.session_id());
        nom::relay<Traits>::pass_response(this, nom::event::feed_event::connected);
        // A response handler may close the connection before subscription.
        if (!connection.is_connected())
            return;
        for (const auto& request : requests_)
        {
            LINFO("Subscribing: {}", request);
            if (!connection.send(request))
            {
                LERROR("Subscription send failed");
                nom::relay<Traits>::pass_response(this, nom::event::feed_event::failed);
                return;
            }
        }
    }

    void operator()(nom::web_socket& connection, const nom::event::ws_data& event)
    {
        // Selected fields, illustrative values; params.channel identifies the subscription:
        // {"jsonrpc":"2.0","method":"channelMessage","params":{"channel":"lightning_executions_BTC_JPY",
        //   "message":[{"id":7,"side":"BUY","price":9000000,"size":0.01,"exec_date":"2026-11-02T00:00:00Z"}]}}
        // https://bf-lightning-api.readme.io/docs/realtime-executions
        // Ticker message is an object, e.g. {"product_code":"BTC_JPY","best_bid":8999999,"ltp":9000000}.
        // Tickers may be throttled; use executions when every trade is needed.
        // https://bf-lightning-api.readme.io/docs/realtime-ticker
        // Board message is an object, e.g. {"mid_price":9000000,"bids":[{"price":8999999,"size":1}],"asks":[]}.
        // The board links above describe both shapes; snapshot levels are not guaranteed sorted.
        // Forward all payloads, including acknowledgements/errors; no parsing or book building here.
        nom::relay<Traits>::pass_response(
            this, nom::event::market_data{.ts = event.ts, .source = connection.session_id(), .data = event.sv_data()});
    }

    void operator()(nom::web_socket&, const nom::event::on_disconnected& event)
    {
        // Realtime subscriptions do not replay the disconnect gap. This example exits; a service that
        // reconnects must resubscribe and restore its book before using incremental data again.
        // https://bf-lightning-api.readme.io/docs/realtime-api
        LWARN("Disconnected: {}", event.reason);
        nom::relay<Traits>::pass_response(this, nom::event::feed_event::failed);
    }

    void operator()(nom::web_socket&, const nom::event::network_error& event)
    {
        LERROR("{}: {}", event.description, event.message);
        nom::relay<Traits>::pass_response(this, nom::event::feed_event::failed);
    }

private:
    std::vector<std::string> requests_;
    nom::web_socket client_;
};

// Keep the terminator before every other stop handler: pass_request only visits later nodes.
struct bitflyer_spot_assembly
    : nom::assembly<bitflyer_spot_assembly, asio_service, app_terminator, market_data_logger, bitflyer_spot_client>
{
};

struct app : nom::relay<bitflyer_spot_assembly>
{
    explicit app(const app_config& config) : nom::relay<bitflyer_spot_assembly>(config)
    {
    }

    int start()
    {
        request(nom::event::service_command::start);
        // Drain canceled handlers before destroying the nodes; do not stop the context early.
        ctx().io.run();
        // Stable stderr summary for tools/asio_comm/conn_test.sh; exclude log and transport overhead.
        std::cerr << "CONN_STATS messages=" << ctx().received_messages
                  << " payload_bytes=" << ctx().received_payload_bytes << '\n';
        return ctx().exit_code.value_or(1);
    }
};

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--help")
    {
        std::cout << "Usage: bitflyer_spot_feeder [--symbols BTC_JPY,ETH_JPY]\n"
                     "                           [--channels ticker,trade,depth] [--endpoint wss://HOST/ws]\n"
                     "Defaults: BTC_JPY, all three channels, wss://ws.lightstream.bitflyer.com/json-rpc.\n"
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
