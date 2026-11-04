// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

#pragma once

import std;
import nom.url;

struct app_config
{
    std::string endpoint{"https://api.bitget.com/api/v3/market/instruments?category=SPOT"};
    // Catalog-only response limit; not an initial allocation or a change to HTTPS defaults.
    unsigned max_response_size{16 * 1024 * 1024};

    [[nodiscard]] std::expected<void, std::string> validate() const
    {
        try
        {
            if (nom::url(endpoint).scheme() != "https")
                return std::unexpected("The endpoint must use https://");
        }
        catch (const std::invalid_argument& error)
        {
            return std::unexpected(std::string(error.what()));
        }
        if (!max_response_size)
            return std::unexpected("--max-response-size must be a positive unsigned byte count");
        return {};
    }
};

inline app_config parse_arguments(int argc, char** argv)
{
    app_config config;
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view option{argv[i]};
        if (option != "--endpoint" && option != "--max-response-size")
            throw std::invalid_argument("Unknown option: " + std::string(option));
        if (++i == argc)
            throw std::invalid_argument("Missing value for " + std::string(option));
        const std::string_view value{argv[i]};
        if (option == "--endpoint")
            config.endpoint = value;
        else
        {
            const auto [end, error] =
                std::from_chars(value.data(), value.data() + value.size(), config.max_response_size);
            if (error != std::errc{} || end != value.data() + value.size())
                throw std::invalid_argument("--max-response-size must be a positive unsigned byte count");
        }
    }
    return config;
}
