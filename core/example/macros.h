// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

#pragma once

#include <cstdio>
#include <format>

// Modules can't export macros, so importers include this header next to `import nom.example;`.
// LPRINT follows the L* logging family: it records the call site, which a function can't do.
#define LPRINT(...)                                                                                                    \
    std::fputs(std::format("[{}:{}] {}\n", __FILE_NAME__, __LINE__, std::format(__VA_ARGS__)).c_str(), stdout)
