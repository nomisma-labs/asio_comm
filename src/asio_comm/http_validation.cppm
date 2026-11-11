// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0

module nom.asio_comm:http_validation;

import std;

namespace nom::detail
{
    // Beast's outgoing field setters check lengths, not character syntax.
    // Field names are HTTP tokens; values permit horizontal tab and opaque bytes,
    // but no other control characters. https://www.rfc-editor.org/rfc/rfc9110#section-5
    bool is_http_header_name(std::string_view name) noexcept
    {
        return !name.empty() && std::ranges::all_of(name,
                                                    [](char c)
                                                    {
                                                        return std::isalnum(c, std::locale::classic()) ||
                                                               std::string_view("!#$%&'*+-.^_`|~").contains(c);
                                                    });
    }

    bool is_http_header_value(std::string_view value) noexcept
    {
        return std::ranges::all_of(value, [](char c) { return c == '\t' || !std::iscntrl(c, std::locale::classic()); });
    }
} // namespace nom::detail
