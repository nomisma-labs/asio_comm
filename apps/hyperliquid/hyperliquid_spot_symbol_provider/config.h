// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

#pragma once

import std;
import nom.spd_logger;
import nom.url;

struct app_config
{
    std::string endpoint{"wss://api.hyperliquid.xyz/ws"};
    // Explicit receive budget, not an initial allocation or the TCP socket buffer size.
    unsigned max_message_size{64 * 1024 * 1024};
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
        return {};
    }
};

inline app_config parse_arguments(int argc, char** argv)
{
    app_config config;
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view option{argv[i]};
        if (option != "--endpoint" && option != "--max-message-size")
            throw std::invalid_argument("Unknown option: " + std::string(option));
        if (++i == argc)
            throw std::invalid_argument("Missing value for " + std::string(option));
        const std::string_view value{argv[i]};
        if (option == "--endpoint")
            config.endpoint = value;
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
