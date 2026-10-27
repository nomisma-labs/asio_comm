# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

# nom_find_catch2()
#
# An installed Catch2 3.16.1 or newer within major version 3, otherwise that release downloaded
# and built in the build tree. Ubuntu packages an older Catch2, so docs/kit/build.md#toolchain
# installs this one; without it every build tree builds its own. SYSTEM keeps Catch2's headers out
# of our warnings, as with an installed package.
macro(nom_find_catch2)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.16.1
        GIT_SHALLOW TRUE
        SYSTEM
        FIND_PACKAGE_ARGS 3.16.1 CONFIG)
    FetchContent_MakeAvailable(Catch2)
endmacro()
