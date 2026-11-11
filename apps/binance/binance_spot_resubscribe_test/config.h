// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0

#pragma once

import std;
import nom.core;

// Normalize a copy for the wire; validation leaves configured/displayed identifiers unchanged.
inline std::string normalized_symbol(std::string symbol)
{
    const auto& facet = std::use_facet<std::ctype<char>>(std::locale::classic());

    facet.tolower(symbol.data(), symbol.data() + symbol.size());
    return symbol;
}

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

    [[nodiscard]] std::expected<void, std::string> validate() const
    {
        // Use the same URL contract as web_socket, reporting invalid input without propagating it.
        try
        {
            if (nom::url(endpoint).scheme() != "wss")
                return std::unexpected("The endpoint must use wss://");
        }
        catch (const std::invalid_argument& error)
        {
            return std::unexpected(std::string(error.what()));
        }
        if (symbols.empty() || channels.empty())
            return std::unexpected("Select at least one symbol and one channel");
        if (resubscribe_after_messages == 0)
            return std::unexpected("--resubscribe-after must be a positive unsigned message count");
        if (resubscribe_symbols.empty())
            return std::unexpected("Select at least one additional symbol");
        for (const auto& channel : channels)
            if (channel != "bookTicker" && channel != "trade" && channel != "depth" && channel != "depth@100ms")
                return std::unexpected("Unknown channel: " + channel +
                                       " (expected bookTicker, trade, depth, depth@100ms)");
        for (const auto* selected : {&symbols, &resubscribe_symbols})
            for (const auto& symbol : *selected)
            {
                if (symbol.empty())
                    return std::unexpected("Symbols must not be empty");
                for (const auto letter : symbol)
                    if (!((letter >= 'A' && letter <= 'Z') || (letter >= 'a' && letter <= 'z') ||
                          (letter >= '0' && letter <= '9')))
                        return std::unexpected("Invalid character in symbol: " + symbol);
            }
        // Selection budget; the request builder maps these aliases to the documented wire channels.
        if (symbols.size() > 1024 / channels.size())
            return std::unexpected("A connection supports at most 1024 streams");
        if (resubscribe_symbols.size() > 1024 / channels.size())
            return std::unexpected("A connection supports at most 1024 streams");
        // SUBSCRIBE adds streams; count the normalized union across both requests.
        std::set<std::string> combined;
        for (const auto* selected : {&symbols, &resubscribe_symbols})
            for (const auto& symbol : *selected)
                combined.insert(normalized_symbol(symbol));
        if (combined.size() > 1024 / channels.size())
            return std::unexpected("A connection supports at most 1024 streams across both subscriptions");
        return {};
    }
};

inline std::vector<std::string> split_list(std::string_view value)
{
    std::vector<std::string> result;
    for (auto item : std::views::split(value, ','))
        result.emplace_back(item.begin(), item.end());
    return result;
}

inline app_config parse_arguments(int argc, char** argv)
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
            if (error != std::errc{} || end != value.data() + value.size())
                throw std::invalid_argument("--resubscribe-after must be a positive unsigned message count");
        }
        else
            config.endpoint = argv[i];
    }
    return config;
}
