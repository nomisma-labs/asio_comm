// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0

#pragma once

import std;
import nom.url;

struct app_config
{
    // An interrupted one-shot request must not report a successful catalog fetch.
    static constexpr bool require_completion = true;
    // Unset adds no runtime limit; zero closes immediately after connection, before requesting data.
    std::optional<unsigned> exit_after_sec{};
    std::string endpoint{"https://sapi.xt.com/v4/public/symbol"};
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
        if (option != "--exit-after-sec" && option != "--endpoint" && option != "--max-response-size")
            throw std::invalid_argument("Unknown option: " + std::string(option));
        if (++i == argc)
            throw std::invalid_argument("Missing value for " + std::string(option));
        const std::string_view value{argv[i]};
        if (option == "--endpoint")
            config.endpoint = value;
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
                std::from_chars(value.data(), value.data() + value.size(), config.max_response_size);
            if (error != std::errc{} || end != value.data() + value.size())
                throw std::invalid_argument("--max-response-size must be a positive unsigned byte count");
        }
    }
    return config;
}
