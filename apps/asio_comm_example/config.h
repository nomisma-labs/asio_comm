// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0

#pragma once

import std;
import nom.spd_logger;

namespace example
{
    struct app_config
    {
        std::string_view app_name{"asio_comm_example"};
        std::string_view message{"Hello from asio_comm"};
        nom::log_config log{.level = nom::log_level::info};

        [[nodiscard]] std::expected<void, std::string> validate() const
        {
            if (app_name.empty())
                return std::unexpected("The application name must not be empty");
            return {};
        }
    };

} // namespace example
