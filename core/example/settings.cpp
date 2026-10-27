// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

module;

#include <nlohmann/json.hpp>

#include <expected>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

module nom.example;

namespace nom::example
{
    std::expected<settings, std::string> parse_settings(std::string_view json_text)
    {
        const auto json = nlohmann::json::parse(json_text, nullptr, /*allow_exceptions=*/false);
        if (json.is_discarded())
            return std::unexpected("invalid JSON");

        try
        {
            const settings defaults;
            return settings{.name = json.value("name", defaults.name), .repeat = json.value("repeat", defaults.repeat)};
        }
        catch (const nlohmann::json::exception& error)
        {
            return std::unexpected("invalid settings: " + std::string(error.what()));
        }
    }

    std::string serialize_settings(const settings& value)
    {
        return nlohmann::json{{"name", value.name}, {"repeat", value.repeat}}.dump();
    }

    std::expected<settings, std::string> load_settings(const std::filesystem::path& path)
    {
        std::ifstream file(path);
        if (!file)
            return std::unexpected("cannot open " + path.string());

        const std::string contents{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        auto parsed = parse_settings(contents);
        if (!parsed)
            return std::unexpected(parsed.error() + " in " + path.string());
        return parsed;
    }
} // namespace nom::example
