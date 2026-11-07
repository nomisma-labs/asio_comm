// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

#pragma once

import std;
import nom.spd_logger;
import nom.url;

// Normalize a copy for the wire; validation leaves configured/displayed identifiers unchanged.
inline std::string normalized_symbol(std::string symbol)
{
    const auto& facet = std::use_facet<std::ctype<char>>(std::locale::classic());

    facet.tolower(symbol.data(), symbol.data() + symbol.size());
    return symbol;
}

struct app_config
{
    static constexpr bool require_completion = false;
    std::string endpoint{"wss://stream-sbe.binance.com/ws"};
    // CLI symbols may use either case; normalized_symbol converts copies to lowercase wire identifiers.
    std::vector<std::string> symbols{"BTCUSDT"};
    std::vector<std::string> channels{"ticker", "trade", "depth"};
    // Explicit receive budget, not an initial allocation or the TCP socket buffer size.
    unsigned max_message_size{16 * 1024 * 1024};
    // Populated from BINANCE_SBE_API_KEY by parse_arguments; never logged.
    std::string api_key;
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
        if (symbols.size() > 1024 / channels.size())
            return std::unexpected("Select at most 1024 symbol/channel pairs");
        if (api_key.empty())
            return std::unexpected("Set BINANCE_SBE_API_KEY to an Ed25519 API key");
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
        if (option != "--endpoint" && option != "--max-message-size" && option != "--symbols" &&
            option != "--channels" && option != "--exit-after-sec")
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
        else
        {
            const auto [end, error] =
                std::from_chars(value.data(), value.data() + value.size(), config.max_message_size);
            if (error != std::errc{} || end != value.data() + value.size())
                throw std::invalid_argument("--max-message-size must be a positive unsigned byte count");
        }
    }
    if (const auto* key = std::getenv("BINANCE_SBE_API_KEY"))
        config.api_key = key;
    return config;
}
