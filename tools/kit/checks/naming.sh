#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary
#
# Checks naming (.clang-tidy) over the clang preset's compilation database. It builds first:
# clang-tidy needs the compiled module interfaces to parse code that imports them.
set -euo pipefail
cd "$(dirname "$0")/../../.."

# Include excluded apps too: clang-tidy needs their module interfaces built.
cmake --preset clang -DNOM_BUILD_PRESET=complete > /dev/null
cmake --build --preset clang > /dev/null
if [[ ! -f build/clang/compile_commands.json ]]; then
    echo "no C++ to check yet" # CMake writes no database for a repo without sources
    exit 0
fi
"${RUN_CLANG_TIDY:-run-clang-tidy-22}" -quiet -p build/clang "$(pwd)/(src|apps|tests)/"
