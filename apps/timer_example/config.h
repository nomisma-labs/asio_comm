// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0

#pragma once

import std;

struct app_config
{
    unsigned delay_ms{100};
    unsigned ticks{3};

    [[nodiscard]] std::expected<void, std::string> validate() const
    {
        if (!delay_ms || delay_ms > 60'000)
            return std::unexpected("Delay must be in 1..60000 milliseconds");
        if (!ticks || ticks > 100)
            return std::unexpected("Tick count must be in 1..100");
        return {};
    }
};

inline app_config parse_arguments(int argc, char** argv)
{
    if (argc > 3)
        throw std::invalid_argument("Expected an optional delay and tick count");
    app_config config;
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view text(argv[i]);
        auto& value = i == 1 ? config.delay_ms : config.ticks;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (error != std::errc{} || end != text.data() + text.size())
            throw std::invalid_argument("Timer arguments must be unsigned integers");
    }
    return config;
}
