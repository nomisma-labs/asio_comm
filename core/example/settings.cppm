// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

module;

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>

export module nom.example:settings;

export namespace nom::example
{
    struct settings
    {
        std::string name = "world";
        int repeat = 1;
    };

    // The API uses standard types; the implementation owns the JSON dependency.
    [[nodiscard]] std::expected<settings, std::string> parse_settings(std::string_view json_text);
    [[nodiscard]] std::string serialize_settings(const settings& value);
    [[nodiscard]] std::expected<settings, std::string> load_settings(const std::filesystem::path& path);
} // namespace nom::example
