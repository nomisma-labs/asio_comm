// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0

#pragma once

import std;
import nom.core;

struct app_config
{
    std::string endpoint{"https://example.com/"};
    unsigned requests{1};

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
        if (!requests || requests > 100)
            return std::unexpected("Request count must be in 1..100");
        return {};
    }
};

inline app_config parse_arguments(int argc, char** argv)
{
    app_config config;
    if (argc > 3)
        throw std::invalid_argument("Expected an optional HTTPS URL and request count");
    if (argc > 1)
        config.endpoint = argv[1];
    if (argc > 2)
    {
        const std::string_view value(argv[2]);
        const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), config.requests);
        if (error != std::errc{} || end != value.data() + value.size())
            throw std::invalid_argument("Invalid request count");
    }
    return config;
}
