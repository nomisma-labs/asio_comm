// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

#pragma once

import std;
import nom.spd_logger;
import nom.url;

// Normalize a copy for the wire; validation leaves configured/displayed identifiers unchanged.
inline std::string normalized_symbol(std::string symbol)
{
    for (auto& letter : symbol)
        letter = std::toupper(letter, std::locale::classic());
    return symbol;
}

struct app_config
{
    std::string endpoint{"wss://ws-feed.exchange.coinbase.com"};
    // CLI symbols may use either case; normalized_symbol converts copies to uppercase wire identifiers.
    std::vector<std::string> symbols{"BTC-USD"};
    std::vector<std::string> channels{"ticker", "trade", "depth"};
    // Explicit receive budget, not an initial allocation or the TCP socket buffer size.
    unsigned max_message_size{16 * 1024 * 1024};
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
        if (max_message_size == 0)
            return std::unexpected("--max-message-size must be a positive unsigned byte count");
        if (symbols.empty() || channels.empty())
            return std::unexpected("Select at least one symbol and one channel");
        for (const auto& channel : channels)
            if (channel != "ticker" && channel != "trade" && channel != "depth")
                return std::unexpected("Unknown channel: " + channel + " (expected ticker, trade, depth)");
        for (const auto& symbol : symbols)
        {
            if (symbol.empty())
                return std::unexpected("Symbols must not be empty");
            for (const auto letter : symbol)
                if (!((letter >= 'A' && letter <= 'Z') || (letter >= 'a' && letter <= 'z') ||
                      (letter >= '0' && letter <= '9') || std::string_view("-_/.:@").contains(letter)))
                    return std::unexpected("Invalid character in symbol: " + symbol);
        }
        // Selection budget; the request builder maps these aliases to the documented wire channels.
        if (symbols.size() > 100 / channels.size())
            return std::unexpected("Select at most 100 symbol/channel pairs");
        return {};
    }
};

inline std::vector<std::string> split_list(std::string_view value)
{
    std::vector<std::string> values;
    for (;;)
    {
        const auto end = value.find(',');
        const auto part = value.substr(0, end);
        if (part.empty())
            throw std::invalid_argument("List entries must not be empty");
        values.emplace_back(part);
        if (end == value.npos)
            break;
        value.remove_prefix(end + 1);
    }
    return values;
}

inline app_config parse_arguments(int argc, char** argv)
{
    app_config config;
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view option{argv[i]};
        if (option != "--endpoint" && option != "--max-message-size" && option != "--symbols" && option != "--channels")
            throw std::invalid_argument("Unknown option: " + std::string(option));
        if (++i == argc)
            throw std::invalid_argument("Missing value for " + std::string(option));
        const std::string_view value{argv[i]};
        if (option == "--endpoint")
            config.endpoint = value;
        else if (option == "--symbols")
            config.symbols = split_list(value);
        else if (option == "--channels")
            config.channels = split_list(value);
        else
        {
            const auto [end, error] =
                std::from_chars(value.data(), value.data() + value.size(), config.max_message_size);
            if (error != std::errc{} || end != value.data() + value.size())
                throw std::invalid_argument("--max-message-size must be a positive unsigned byte count");
        }
    }
    return config;
}
