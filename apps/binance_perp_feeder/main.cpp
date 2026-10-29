// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

// GCC 16's Ubuntu snapshot needs stdio declarations before module imports at -O2.
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ == 16
#include <cstdio>
#endif

#include <cassert>
#include <spd_logger/logging.h>

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
        std::string market_endpoint{"wss://fstream.binance.com/market/ws"};
        std::string public_endpoint{"wss://fstream.binance.com/public/ws"};
        std::vector<std::string> symbols{"BTCUSDT"};
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
            if (option != "--symbols" && option != "--channels" && option != "--market-endpoint" &&
                option != "--public-endpoint")
                throw std::invalid_argument("Unknown option: " + std::string(option));
            if (++i == argc)
                throw std::invalid_argument("Missing value for " + std::string(option));
            if (option == "--symbols")
                config.symbols = split_list(argv[i]);
            else if (option == "--channels")
                config.channels = split_list(argv[i]);
            else if (option == "--market-endpoint")
                config.market_endpoint = argv[i];
            else
                config.public_endpoint = argv[i];
        }
        return config;
    }

    struct subscription
    {
        std::string endpoint;
        std::vector<std::string> streams;

        std::string request() const
        {
            std::string result = R"({"method":"SUBSCRIBE","params":[)";
            bool first = true;
            for (const auto& stream : streams)
            {
                if (!first)
                    result += ',';
                first = false;
                result += '"' + stream + '"';
            }
            result += R"(],"id":1})";
            return result;
        }
    };

    std::array<subscription, 2> make_subscriptions(const app_config& config)
    {
        if (config.symbols.empty() || config.channels.empty())
            throw std::invalid_argument("Select at least one symbol and one channel");
        for (const auto& channel : config.channels)
        {
            if (channel != "ticker" && channel != "trade" && channel != "depth")
                throw std::invalid_argument("Unknown channel: " + channel + " (expected ticker, trade or depth)");
        }

        std::array<subscription, 2> result;
        result[0].endpoint = config.market_endpoint;
        result[1].endpoint = config.public_endpoint;
        for (auto symbol : config.symbols)
        {
            if (symbol.empty())
                throw std::invalid_argument("Symbols must not be empty");
            for (char& letter : symbol)
            {
                if (letter >= 'A' && letter <= 'Z')
                    letter = static_cast<char>(letter - 'A' + 'a');
                else if (!(letter >= 'a' && letter <= 'z') && !(letter >= '0' && letter <= '9'))
                    throw std::invalid_argument("Symbols must contain only ASCII letters and digits");
            }
            // Validated symbols and stream names need no JSON escaping.
            for (const auto& channel : config.channels)
            {
                auto& group = result[channel == "depth" ? 1 : 0];
                const std::string stream_name = channel == "trade" ? "aggTrade" : channel;
                group.streams.push_back(symbol + '@' + stream_name);
                if (group.streams.size() > 1024)
                    throw std::invalid_argument("A connection supports at most 1024 streams");
            }
        }
        return result;
    }

    struct feed
    {
        feed(nom::asio::any_io_executor executor, const subscription& selected)
            : client(executor, nom::url(selected.endpoint)), request(selected.request())
        {
            // Server pings are three minutes apart; periodic client pings elicit pongs for the idle timer.
            client.params().enable_outbound_ping = true;
        }

        nom::web_socket client;
        std::string request;
    };

    template <typename Traits> struct asio_service
    {
        struct context_mixin
        {
            // The assembly constructs context services before nodes and destroys them after nodes.
            nom::asio::io_context io;
        };
    };

    template <typename Traits> struct market_data_logger
    {
        void on_response(const nom::event::market_data& event)
        {
            LINFO("{} {}", event.source, event.data);
        }
    };

    template <typename Traits> class binance_perp_client
    {
    public:
        explicit binance_perp_client(const app_config& config) : connect_deadline_(nom::ctx(this).io)
        {
            for (const auto& selected : make_subscriptions(config))
            {
                if (!selected.streams.empty())
                    feeds_.push_back(std::make_unique<feed>(nom::ctx(this).io.get_executor(), selected));
            }
        }

        void on_request(nom::event::service_command command)
        {
            switch (command)
            {
            case nom::event::service_command::start:
            {
                running_ = true;
                pending_connections_ = feeds_.size();
                connect_deadline_.expires_after(std::chrono::seconds(30));
                connect_deadline_.async_wait(
                    [this](const nom::asio::error_code& error)
                    {
                        if (!error)
                        {
                            LERROR("Connection setup timed out");
                            on_request(nom::event::service_command::stop);
                        }
                    });
                for (const auto& current : feeds_)
                {
                    // A synchronous setup failure can stop the whole feeder during connect().
                    if (!running_)
                        break;
                    LINFO("Connecting to {}", current->client.url().target());
                    current->client.connect(nom::web_socket::delegate{std::ref(*this)});
                }
                break;
            }
            case nom::event::service_command::stop:
                running_ = false;
                connect_deadline_.cancel();
                for (const auto& current : feeds_)
                    current->client.close();
                break;
            }
        }

        void operator()(nom::web_socket& connection, const nom::event::on_connected&)
        {
            if (--pending_connections_ == 0)
                connect_deadline_.cancel();
            LINFO("Connected: {}", connection.session_id());
            const auto selected =
                std::ranges::find_if(feeds_, [&](const auto& current) { return &current->client == &connection; });
            assert(selected != feeds_.end());
            LINFO("{} Subscribing: {}", connection.session_id(), (*selected)->request);
            if (!connection.send((*selected)->request))
            {
                LERROR("{} Subscription send failed", connection.session_id());
                on_request(nom::event::service_command::stop);
            }
        }

        void operator()(nom::web_socket& connection, const nom::event::ws_data& event)
        {
            nom::relay<Traits>::pass_response(
                this,
                nom::event::market_data{.ts = event.ts, .source = connection.session_id(), .data = event.sv_data()});
        }

        void operator()(nom::web_socket& connection, const nom::event::on_disconnected& event)
        {
            LWARN("{} Disconnected: {}", connection.session_id(), event.reason);
            on_request(nom::event::service_command::stop);
        }

        void operator()(nom::web_socket& connection, const nom::event::network_error& event)
        {
            LERROR("{} {}: {}", connection.session_id(), event.description, event.message);
            on_request(nom::event::service_command::stop);
        }

    private:
        std::vector<std::unique_ptr<feed>> feeds_;
        nom::asio::steady_timer connect_deadline_;
        std::size_t pending_connections_{};
        bool running_{false};
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
} // namespace

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--help")
    {
        std::cout << "Usage: binance_perp_feeder [--symbols BTCUSDT,ETHUSDT] [--channels ticker,trade,depth]\n"
                     "                           [--market-endpoint wss://HOST/market/ws]\n"
                     "                           [--public-endpoint wss://HOST/public/ws]\n"
                     "Defaults: BTCUSDT, all three channels, Binance USD-M futures routed endpoints.\n"
                     "trade selects aggTrade; ticker/trade use /market and depth uses /public.\n"
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
