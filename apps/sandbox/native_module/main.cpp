// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

#include <cstdio>

import nom.sandbox.geometry;

int main()
{
    const nom::sandbox::point origin;
    const nom::sandbox::point target{3, 4};
    std::printf("distance = %g\n", nom::sandbox::distance(origin, target));
    return 0;
}
