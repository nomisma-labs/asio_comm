// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

// GCC 16's Ubuntu snapshot needs stdio declarations before module imports at -O2.
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ == 16
#include <cstdio>
#endif

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
        std::string endpoint{"wss://stream.binance.com:443/ws"};
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
        if (config.symbols.empty() || config.channels.empty())
            throw std::invalid_argument("Select at least one symbol and one channel");
        if (config.symbols.size() > 1024 / config.channels.size())
            throw std::invalid_argument("A connection supports at most 1024 streams");
        for (const auto& channel : config.channels)
        {
            if (channel != "ticker" && channel != "trade" && channel != "depth")
                throw std::invalid_argument("Unknown channel: " + channel + " (expected ticker, trade or depth)");
        }

        std::string request = R"({"method":"SUBSCRIBE","params":[)";
        bool first = true;
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
            nom::asio::io_context io;
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
            : requests_({subscription_request(config)}),
              client_(nom::ctx(this).io.get_executor(), nom::url(config.endpoint)), connect_deadline_(nom::ctx(this).io)
        {
        }

        void on_request(nom::event::service_command command)
        {
            switch (command)
            {
            case nom::event::service_command::start:
            {
                connect_deadline_.expires_after(std::chrono::seconds(30));
                connect_deadline_.async_wait(
                    [this](const nom::asio::error_code& error)
                    {
                        if (!error)
                        {
                            LERROR("Connection timed out");
                            on_request(nom::event::service_command::stop);
                        }
                    });
                std::ostringstream address;
                address << client_.url();
                LINFO("Connecting to {}", address.str());
                client_.connect(nom::web_socket::delegate{std::ref(*this)});
                break;
            }
            case nom::event::service_command::stop:
                connect_deadline_.cancel();
                client_.close();
                break;
            }
        }

        void operator()(nom::web_socket& connection, const nom::event::on_connected&)
        {
            connect_deadline_.cancel();
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
            nom::relay<Traits>::pass_response(
                this,
                nom::event::market_data{.ts = event.ts, .source = connection.session_id(), .data = event.sv_data()});
        }

        void operator()(nom::web_socket&, const nom::event::on_disconnected& event)
        {
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
        nom::asio::steady_timer connect_deadline_;
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
                     "                           [--channels ticker,trade,depth] [--endpoint wss://HOST/ws]\n"
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
