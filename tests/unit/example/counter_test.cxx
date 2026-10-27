// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

#include <catch2/catch_test_macros.hpp>

import nom.example;

TEST_CASE("counter counts and resets", "[counter]")
{
    nom::example::counter clicks;
    CHECK(clicks.value() == 0);

    clicks.increment();
    clicks.increment();
    CHECK(clicks.value() == 2);

    clicks.reset();
    CHECK(clicks.value() == 0);
}
