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
        // Public streaming endpoint; /ws delivers unwrapped events for all subscribed streams.
        // https://github.com/binance/binance-spot-api-docs/blob/master/web-socket-streams.md#general-wss-information
        std::string endpoint{"wss://stream.binance.com:443/ws"};
        // Uppercase defaults match displayed symbols; CLI input may use either case. subscription_request
        // lowercases a copy for stream names (BTCUSDT -> btcusdt@trade), preserving bookTicker's case.
        std::vector<std::string> symbols{"BTCUSDT"};
        // bookTicker = best quotes; trade = individual executions. depth@100ms selects faster
        // diff delivery than depth's 1000 ms default, at the cost of more traffic; it is not a snapshot.
        // https://github.com/binance/binance-spot-api-docs/blob/master/web-socket-streams.md#diff-depth-stream
        std::vector<std::string> channels{"bookTicker", "trade", "depth@100ms"};
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
        if (config.symbols.empty() || config.channels.empty())
            throw std::invalid_argument("Select at least one symbol and one channel");
        if (config.symbols.size() > 1024 / config.channels.size())
            throw std::invalid_argument("A connection supports at most 1024 streams");
        for (const auto& channel : config.channels)
        {
            if (channel != "bookTicker" && channel != "trade" && channel != "depth" && channel != "depth@100ms")
                throw std::invalid_argument("Unknown channel: " + channel +
                                            " (expected bookTicker, trade, depth or depth@100ms)");
        }

        std::string request = R"({"method":"SUBSCRIBE","params":[)";
        bool first = true;
        for (auto symbol : config.symbols)
        {
            if (symbol.empty())
                throw std::invalid_argument("Symbols must not be empty");
            // This example accepts ASCII symbols; the classic locale keeps wire casing locale-independent.
            for (char& letter : symbol)
            {
                if (letter >= 'A' && letter <= 'Z')
                    letter = std::tolower(letter, std::locale::classic());
                else if (!(letter >= 'a' && letter <= 'z') && !(letter >= '0' && letter <= '9'))
                    throw std::invalid_argument("Symbols must contain only ASCII letters and digits");
            }
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

    template <typename Traits> class binance_spot_client
    {
    public:
        explicit binance_spot_client(const app_config& config)
            : request_(subscription_request(config)),
              client_(nom::ctx(this).io.get_executor(), nom::url(config.endpoint))
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
            // Workflow: connect -> SUBSCRIBE -> {"result":null,"id":1} or error -> continuous data.
            // send() confirms the local write only; this logger forwards replies without validating the ack.
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
                this,
                nom::event::market_data{.ts = event.ts, .source = connection.session_id(), .data = event.sv_data()});
        }

        void operator()(nom::web_socket&, const nom::event::on_disconnected& event)
        {
            // While reading, Beast answers server control Pings automatically; no application ping is needed.
            // Binance rotates connections after 24 h. This example exits; a continuous service must reconnect,
            // resubscribe and restore book state. Connection rules are linked beside app_config::endpoint.
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

    struct binance_spot_assembly
        : nom::assembly<binance_spot_assembly, asio_service, market_data_logger, binance_spot_client>
    {
    };

    struct app : nom::relay<binance_spot_assembly>
    {
        explicit app(const app_config& config) : nom::relay<binance_spot_assembly>(config)
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
        std::cout << "Usage: binance_spot_feeder [--symbols BTCUSDT,ETHUSDT]\n"
                     "                           [--channels bookTicker,trade,depth@100ms] [--endpoint wss://HOST/ws]\n"
                     "Defaults: BTCUSDT, all three channels, wss://stream.binance.com:443/ws.\n"
                     "Log all incoming messages until disconnected or stopped with Ctrl+C.\n";
        return 0;
    }

    try
    {
        const auto config = parse_arguments(argc, argv);
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
