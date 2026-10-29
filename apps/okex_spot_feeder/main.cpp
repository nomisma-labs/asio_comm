// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

// GCC 16's Ubuntu snapshot needs stdio declarations before module imports at -O2.
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ == 16
#include <cstdio>
#endif

#include <spd_logger/logging.h>

import std;
import nom.asio_comm;
import nom.spd_logger;

namespace
{
    struct app_config
    {
        std::string endpoint{"wss://ws.okx.com/ws/v5/public"};
        std::vector<std::string> symbols{"BTC-USDT"};
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
            for (char& letter : symbol)
            {
                if (letter >= 'a' && letter <= 'z')
                    letter = static_cast<char>(letter - 'a' + 'A');
                else if (!(letter >= 'A' && letter <= 'Z') && !(letter >= '0' && letter <= '9') && letter != '-')
                    throw std::invalid_argument("Invalid character in symbol: " + symbol);
            }
            if (std::ranges::count(symbol, '-') != 1 || symbol.front() == '-' || symbol.back() == '-')
                throw std::invalid_argument("Expected a spot symbol, e.g. BTC-USDT");
        }
    }

    std::vector<std::string> subscription_requests(const app_config& config)
    {
        std::string request = R"({"op":"subscribe","args":[)";
        bool first = true;
        for (const auto& symbol : config.symbols)
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
        if (request.size() > 64 * 1024)
            throw std::invalid_argument("The subscription request exceeds OKX's 64 KiB limit");
        return {request};
    }
} // namespace

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
        auto config = parse_arguments(argc, argv);
        validate_config(config);
        const auto requests = subscription_requests(config);
        nom::init_logger(nom::main_logger, config.log);
        nom::asio::io_context io;
        nom::web_socket client(io.get_executor(), nom::url(config.endpoint));
        nom::asio::steady_timer connect_deadline(io, std::chrono::seconds(30));
        nom::asio::steady_timer ping_timer(io);
        const auto finish = [&]
        {
            connect_deadline.cancel();
            ping_timer.cancel();
            client.close();
        };
        connect_deadline.async_wait(
            [&](const nom::asio::error_code& error)
            {
                if (!error)
                {
                    LERROR("Connection timed out");
                    finish();
                }
            });

        // OKX uses text ping/pong, separate from WebSocket control frames.
        std::function<void()> start_ping_timer;
        start_ping_timer = [&]
        {
            ping_timer.expires_after(std::chrono::seconds(20));
            ping_timer.async_wait(
                [&](const nom::asio::error_code& error)
                {
                    if (error || !client.is_connected())
                        return;
                    if (!client.send("ping"))
                    {
                        LERROR("Heartbeat send failed");
                        finish();
                        return;
                    }
                    start_ping_timer();
                });
        };

        LINFO("Connecting to {}", config.endpoint);
        // One io_context thread serializes client calls and callbacks.
        const auto on_event = [&](nom::web_socket& connection, const auto& event)
        {
            using event_type = std::remove_cvref_t<decltype(event)>;
            if constexpr (std::same_as<event_type, nom::event::on_connected>)
            {
                connect_deadline.cancel();
                LINFO("Connected: {}", connection.session_id());
                for (const auto& request : requests)
                {
                    LINFO("Subscribing: {}", request);
                    if (!connection.send(request))
                    {
                        LERROR("Subscription send failed");
                        finish();
                        return;
                    }
                }
                start_ping_timer();
            }
            else if constexpr (std::same_as<event_type, nom::event::ws_data>)
            {
                // The logger consumes the borrowed payload before this callback returns.
                LINFO("{}", event.sv_data());
            }
            else if constexpr (std::same_as<event_type, nom::event::on_disconnected>)
            {
                LWARN("Disconnected: {}", event.reason);
                finish();
            }
            else if constexpr (std::same_as<event_type, nom::event::network_error>)
            {
                LERROR("{}: {}", event.description, event.message);
                finish();
            }
        };
        client.connect(nom::web_socket::delegate{on_event});

        io.run();
        nom::flush_logger(nom::main_logger);
        return 1; // An ended connection interrupts this continuous feed.
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
