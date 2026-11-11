// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0

#pragma once

import std;
import nom.core;

// Normalize a copy for the wire; validation leaves configured/displayed identifiers unchanged.
inline std::string normalized_symbol(std::string symbol)
{
    const auto& facet = std::use_facet<std::ctype<char>>(std::locale::classic());

    facet.toupper(symbol.data(), symbol.data() + symbol.size());
    return symbol;
}

struct app_config
{
    static constexpr bool require_completion = false;
    // Production JSON-RPC WebSocket; these public channels need no credentials.
    // BTC-PERPETUAL is the example inverse perpetual instrument, not a Spot symbol.
    // https://docs.deribit.com/articles/deribit-quickstart
    std::string endpoint{"wss://www.deribit.com/ws/api/v2"};
    // CLI symbols may use either case; normalized_symbol converts the instrument to uppercase
    // (btc-perpetual -> BTC-PERPETUAL). Channel prefixes/suffixes stay lowercase, e.g. book.*.100ms.
    std::vector<std::string> symbols{"BTC-PERPETUAL"};
    std::vector<std::string> channels{"ticker", "trade", "depth"};
    // Unset runs indefinitely; zero closes immediately after the WebSocket handshake.
    std::optional<unsigned> exit_after_sec{};
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
        for (const auto& channel : channels)
            if (channel != "ticker" && channel != "trade" && channel != "depth")
                return std::unexpected("Unknown channel: " + channel + " (expected ticker, trade, depth)");
        for (const auto& symbol : symbols)
        {
            if (symbol.empty())
                return std::unexpected("Symbols must not be empty");
            for (const auto letter : symbol)
                if (!((letter >= 'A' && letter <= 'Z') || (letter >= 'a' && letter <= 'z') ||
                      (letter >= '0' && letter <= '9') || std::string_view("-_").contains(letter)))
                    return std::unexpected("Invalid character in symbol: " + symbol);
            if (!normalized_symbol(symbol).ends_with("-PERPETUAL"))
                return std::unexpected("Expected a perpetual instrument, e.g. BTC-PERPETUAL");
        }
        // Selection budget; the request builder maps these aliases to the documented wire channels.
        if (symbols.size() > 500 / channels.size())
            return std::unexpected("Select at most 500 symbol/channel pairs");
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
        if (option != "--symbols" && option != "--channels" && option != "--endpoint" && option != "--exit-after-sec")
            throw std::invalid_argument("Unknown option: " + std::string(option));
        if (++i == argc)
            throw std::invalid_argument("Missing value for " + std::string(option));
        if (option == "--symbols")
            config.symbols = split_list(argv[i]);
        else if (option == "--channels")
            config.channels = split_list(argv[i]);
        else if (option == "--endpoint")
            config.endpoint = argv[i];
        else if (option == "--exit-after-sec")
        {
            const std::string_view seconds_text{argv[i]};
            unsigned seconds{};
            const auto [end, error] =
                std::from_chars(seconds_text.data(), seconds_text.data() + seconds_text.size(), seconds);
            if (error != std::errc{} || end != seconds_text.data() + seconds_text.size())
                throw std::invalid_argument("--exit-after-sec must be an unsigned number of seconds (0 is allowed)");
            config.exit_after_sec = seconds;
        }
    }
    return config;
}
