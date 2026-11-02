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
        // Use the same raw /ws protocol as binance_spot_feeder; trade gives one event per execution
        // for this receive-callback test. The second request adds ETHUSDT on the existing connection.
        // https://github.com/binance/binance-spot-api-docs/blob/master/web-socket-streams.md#trade-streams
        std::string endpoint{"wss://stream.binance.com:443/ws"};
        // Both symbol lists use conventional uppercase defaults; normalized_symbol converts copies to
        // lowercase stream names (ETHUSDT -> ethusdt@trade), leaving channel names case-sensitive.
        std::vector<std::string> symbols{"BTCUSDT"};
        std::vector<std::string> channels{"trade"};
        unsigned resubscribe_after_messages{3};
        std::vector<std::string> resubscribe_symbols{"ETHUSDT"};
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
            if (option != "--symbols" && option != "--channels" && option != "--endpoint" &&
                option != "--resubscribe-after" && option != "--resubscribe-symbols")
                throw std::invalid_argument("Unknown option: " + std::string(option));
            if (++i == argc)
                throw std::invalid_argument("Missing value for " + std::string(option));
            if (option == "--symbols")
                config.symbols = split_list(argv[i]);
            else if (option == "--channels")
                config.channels = split_list(argv[i]);
            else if (option == "--resubscribe-symbols")
                config.resubscribe_symbols = split_list(argv[i]);
            else if (option == "--resubscribe-after")
            {
                const std::string_view value{argv[i]};
                const auto [end, error] =
                    std::from_chars(value.data(), value.data() + value.size(), config.resubscribe_after_messages);
                if (error != std::errc{} || end != value.data() + value.size() ||
                    config.resubscribe_after_messages == 0)
                    throw std::invalid_argument("--resubscribe-after must be a positive unsigned message count");
            }
            else
                config.endpoint = argv[i];
        }
        return config;
    }

    std::string normalized_symbol(std::string symbol)
    {
        if (symbol.empty())
            throw std::invalid_argument("Symbols must not be empty");
        // Use the same normalized spelling for subscription requests and duplicate-symbol counting.
        // The classic locale fixes case conversion for the ASCII symbols accepted by this example.
        for (char& letter : symbol)
        {
            if (letter >= 'A' && letter <= 'Z')
                letter = std::tolower(letter, std::locale::classic());
            else if (!(letter >= 'a' && letter <= 'z') && !(letter >= '0' && letter <= '9'))
                throw std::invalid_argument("Symbols must contain only ASCII letters and digits");
        }
        return symbol;
    }

    std::string subscription_request(const app_config& config, unsigned id)
    {
        // Initial: {"method":"SUBSCRIBE","params":["btcusdt@trade"],"id":1}.
        // Follow-up: {"method":"SUBSCRIBE","params":["ethusdt@trade"],"id":2}; SUBSCRIBE adds streams.
        // IDs distinguish replies; removing BTC would require a separate UNSUBSCRIBE.
        // https://github.com/binance/binance-spot-api-docs/blob/master/web-socket-streams.md#live-subscribingunsubscribing-to-streams
        // Other supported channel choices and depth cadence are explained in binance_spot_feeder/main.cpp.
        // Limits: 1024 active streams/connection; 5 client-to-server messages/s, counting JSON commands
        // and control Ping/Pong frames. Each subscription below is one command, regardless of symbol count.
        // https://github.com/binance/binance-spot-api-docs/blob/master/web-socket-streams.md#websocket-limits
        // Check each request here and the cumulative stream count in resubscription_request().
        // Neither request is split into batches; this diagnostic has no rate limiter.
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
        for (const auto& value : config.symbols)
        {
            const auto symbol = normalized_symbol(value);
            // Validated symbols and channel names need no JSON escaping.
            for (const auto& channel : config.channels)
            {
                if (!first)
                    request += ',';
                first = false;
                request += '"' + symbol + '@' + channel + '"';
            }
        }
        request += "],\"id\":" + std::to_string(id) + '}';
        return request;
    }

    std::string resubscription_request(const app_config& config)
    {
        if (config.resubscribe_after_messages == 0)
            throw std::invalid_argument("The resubscription message count must be positive");
        if (config.resubscribe_symbols.empty())
            throw std::invalid_argument("Select at least one additional symbol");

        auto next = config;
        next.symbols = config.resubscribe_symbols;
        auto request = subscription_request(next, 2);
        // SUBSCRIBE adds to the existing selection. Count the normalized symbol union across both
        // requests, multiplied by the shared channel count; repeated channel entries count conservatively.
        std::set<std::string> symbols;
        for (const auto& symbol : config.symbols)
            symbols.insert(normalized_symbol(symbol));
        for (const auto& symbol : next.symbols)
            symbols.insert(normalized_symbol(symbol));
        if (symbols.size() > 1024 / config.channels.size())
            throw std::invalid_argument("A connection supports at most 1024 streams across both subscriptions");
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

    template <typename Traits> class resubscribe_client
    {
    public:
        explicit resubscribe_client(const app_config& config)
            : request_(subscription_request(config, 1)), resubscribe_request_(resubscription_request(config)),
              remaining_messages_(config.resubscribe_after_messages),
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
            // Connect -> initial subscription -> data -> second subscription inside the Nth data callback.
            // Replies are logged, not used as gates; a successful local send is not Binance's acknowledgement.
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
            nom::relay<Traits>::pass_response(
                this,
                nom::event::market_data{.ts = event.ts, .source = connection.session_id(), .data = event.sv_data()});

            // Example trade (selected fields): {"e":"trade","s":"BTCUSDT","t":7,"p":"60000","q":"0.01"}.
            // Acks are {"result":null,"id":1} or id:2; exclude them from the trigger count.
            // This string check is specific to the supported raw messages, not a general JSON classifier.
            // N received updates trigger exactly one extra send; N is not a time-based rate budget.
            if (remaining_messages_ != 0 && event.sv_data().find("\"s\"") != std::string_view::npos &&
                --remaining_messages_ == 0)
            {
                LINFO("Resubscribing from ws_data: {}", resubscribe_request_);
                if (connection.send(resubscribe_request_))
                    LINFO("Resubscription send returned true");
                else
                {
                    LERROR("Resubscription send returned false");
                    on_request(nom::event::service_command::stop);
                }
            }
        }

        void operator()(nom::web_socket&, const nom::event::on_disconnected& event)
        {
            // Beast handles server control Pings while reading. End the diagnostic on disconnection;
            // reconnecting would start a different session and would not extend this re-entry experiment.
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
        std::string resubscribe_request_;
        unsigned remaining_messages_;
        nom::web_socket client_;
    };

    struct resubscribe_assembly
        : nom::assembly<resubscribe_assembly, asio_service, market_data_logger, resubscribe_client>
    {
    };

    struct app : nom::relay<resubscribe_assembly>
    {
        explicit app(const app_config& config) : nom::relay<resubscribe_assembly>(config)
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
        std::cout << "Usage: binance_spot_resubscribe_test [--symbols BTCUSDT] [--channels trade]\n"
                     "                                     [--resubscribe-after N] [--resubscribe-symbols ETHUSDT]\n"
                     "                                     [--endpoint wss://HOST/ws]\n"
                     "Defaults: BTCUSDT trade; add ETHUSDT after 3 market-data messages.\n"
                     "Send directly from the Nth data callback on the same connection.\n"
                     "Keep logging until disconnected or stopped with Ctrl+C.\n";
        return 0;
    }

    try
    {
        const auto config = parse_arguments(argc, argv);
        nom::init_logger(nom::main_logger, config.log);
        app(config).start();
        nom::flush_logger(nom::main_logger);
        return 1; // Disconnection ends this continuous diagnostic run.
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
