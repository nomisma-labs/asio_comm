// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <type_traits>

import nom.example;

static_assert(std::is_same_v<nom::example::type_at_t<0, int, std::string>, int>);
static_assert(std::is_same_v<nom::example::type_at_t<1, int, std::string>, std::string>);

TEST_CASE("join formats and separates its arguments", "[join]")
{
    CHECK(nom::example::join(", ") == "");
    CHECK(nom::example::join(", ", "one") == "one");
    CHECK(nom::example::join(", ", "one", 2, 3.5) == "one, 2, 3.5");
}
