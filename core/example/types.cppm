// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

module;

#include <cstddef>
#include <tuple>

export module nom.example:types;

export namespace nom::example
{
    // The header stays in the global module fragment; importers instantiate this alias.
    template <std::size_t Index, typename... Types> using type_at_t = std::tuple_element_t<Index, std::tuple<Types...>>;
} // namespace nom::example
