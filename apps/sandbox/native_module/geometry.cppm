// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

module;

#include <cmath>

export module nom.sandbox.geometry;

export import :point;

export namespace nom::sandbox
{
    [[nodiscard]] double distance(const point& from, const point& to)
    {
        return std::hypot(to.x - from.x, to.y - from.y);
    }
} // namespace nom::sandbox
