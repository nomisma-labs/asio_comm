// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0

#pragma once

import std;
import nom.url;

struct app_config
{
    std::string endpoint{"wss://ws.postman-echo.com/raw"};
    std::optional<std::string> message{"hello"};

    [[nodiscard]] std::expected<void, std::string> validate() const
    {
        try
        {
            if (nom::url(endpoint).scheme() != "wss")
                return std::unexpected("The endpoint must use wss://");
        }
        catch (const std::invalid_argument& error)
        {
            return std::unexpected(std::string(error.what()));
        }
        return {};
    }
};

inline app_config parse_arguments(int argc, char** argv)
{
    app_config config;
    if (argc > 3)
        throw std::invalid_argument("Expected an optional endpoint and message");
    if (argc > 1)
    {
        config.endpoint = argv[1];
        config.message.reset();
    }
    if (argc == 3)
        config.message = argv[2];
    return config;
}
