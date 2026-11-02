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

namespace
{
    struct app_config
    {
        // Use raw JSON-RPC over WebSocket, which fits nom::web_socket; Socket.IO has additional framing.
        // Product codes such as BTC_JPY identify these public channels; no authentication is needed.
        // https://bf-lightning-api.readme.io/docs/endpoint-json-rpc
        std::string endpoint{"wss://ws.lightstream.bitflyer.com/json-rpc"};
        // CLI symbols may use either case; validate_config normalizes the product code to uppercase
        // (btc_jpy -> BTC_JPY). The channel prefix stays lowercase: lightning_executions_BTC_JPY.
        std::vector<std::string> symbols{"BTC_JPY"};
        std::vector<std::string> channels{"ticker", "trade", "depth"};
        nom::log_config log{.level = nom::log_level::info};
    };

    std::vector<std::string> split_list(std::string_view value)
    {
        std::vector<std::string> result;
        for (auto item : value | std::views::split(','))
            result.emplace_back(item.begin(), item.end());
        return result;
    }

    app_config parse_arguments(int argc, char** argv)
    {
        app_config config;
        for (int i = 1; i < argc; ++i)
        {
            const std::string_view option{argv[i]};
            if (option != "--symbols" && option != "--channels" && option != "--endpoint")
                throw std::invalid_argument("Unknown option: " + std::string(option));
            if (++i == argc)
                throw std::invalid_argument("Missing value for " + std::string(option));
            if (option == "--symbols")
                config.symbols = split_list(argv[i]);
            else if (option == "--channels")
                config.channels = split_list(argv[i]);
            else
                config.endpoint = argv[i];
        }
        return config;
    }

    void validate_config(app_config& config)
    {
        if (config.symbols.empty() || config.channels.empty())
            throw std::invalid_argument("Select at least one symbol and one channel");
        for (const auto& channel : config.channels)
        {
            if (channel != "ticker" && channel != "trade" && channel != "depth")
                throw std::invalid_argument("Unknown channel: " + channel + " (expected ticker, trade or depth)");
        }
        for (auto& symbol : config.symbols)
        {
            if (symbol.empty())
                throw std::invalid_argument("Symbols must not be empty");
            // This example accepts ASCII identifiers; the classic locale fixes their wire casing.
            for (char& letter : symbol)
            {
                if (letter >= 'a' && letter <= 'z')
                    letter = std::toupper(letter, std::locale::classic());
                else if (!(letter >= 'A' && letter <= 'Z') && !(letter >= '0' && letter <= '9') && letter != '_')
                    throw std::invalid_argument("Invalid character in symbol: " + symbol);
            }
        }
    }

    std::vector<std::string> subscription_requests(const app_config& config)
    {
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
            requests.push_back(R"({"jsonrpc":"2.0","method":"subscribe","params":{"channel":")" + channel +
                               R"("},"id":)" + std::to_string(requests.size() + 1) + '}');
        };
        for (const auto& symbol : config.symbols)
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

    template <typename Traits> class bitflyer_spot_client
    {
    public:
        explicit bitflyer_spot_client(const app_config& config)
            : requests_(subscription_requests(config)),
              client_(nom::ctx(this).io.get_executor(), nom::url(config.endpoint))
        {
            // Client control probes are our quiet-feed policy, not a documented exchange-specific interval.
            // Keep reads active for server control frames; no application JSON ping is prescribed here.
            // https://bf-lightning-api.readme.io/docs/realtime-api
            auto params = client_.params();
            params.enable_outbound_ping = true;
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
            // Connect -> subscribe for each channel -> {"jsonrpc":"2.0","id":N,"result":true} or error
            // -> channelMessage notifications. send() is local completion; this logger does not check RPC acks.
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
                this,
                nom::event::market_data{.ts = event.ts, .source = connection.session_id(), .data = event.sv_data()});
        }

        void operator()(nom::web_socket&, const nom::event::on_disconnected& event)
        {
            // Realtime subscriptions do not replay the disconnect gap. This example exits; a service that
            // reconnects must resubscribe and restore its book before using incremental data again.
            // https://bf-lightning-api.readme.io/docs/realtime-api
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

    struct bitflyer_spot_assembly
        : nom::assembly<bitflyer_spot_assembly, asio_service, market_data_logger, bitflyer_spot_client>
    {
    };

    struct app : nom::relay<bitflyer_spot_assembly>
    {
        explicit app(const app_config& config) : nom::relay<bitflyer_spot_assembly>(config)
        {
        }

        void start()
        {
            request(nom::event::service_command::start);
            // Relay calls and all WebSocket callbacks stay on this one io_context thread.
            ctx().io.run();
        }
    };
} // namespace

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--help")
    {
        std::cout << "Usage: bitflyer_spot_feeder [--symbols BTC_JPY,ETH_JPY]\n"
                     "                           [--channels ticker,trade,depth] [--endpoint wss://HOST/ws]\n"
                     "Defaults: BTC_JPY, all three channels, wss://ws.lightstream.bitflyer.com/json-rpc.\n"
                     "Log all incoming messages until disconnected or stopped with Ctrl+C.\n";
        return 0;
    }

    try
    {
        auto config = parse_arguments(argc, argv);
        validate_config(config);
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
