# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

cmake_minimum_required(VERSION 3.30)

# The version lives only here; releases are tagged v<version> (docs/kit/build.md#versioning).
project(asio_comm
    VERSION 0.1.0
    DESCRIPTION "C++ communication protocols for cryptocurrency exchanges"
    LANGUAGES CXX)

include(cmake/kit/nom_kit.cmake)
nom_project_options()

# Dependencies come from external/ submodules (docs/kit/howto.md#a-dependency). In a superbuild the
# outermost FetchContent_Declare wins, so every repo shares one copy.

# Boost comes from system packages (docs/kit/build.md#dependencies).
nom_find_boost()

# Discover Catch2 before any code directory can register an inline test suite.
if(nom_build_tests)
    enable_testing()
    nom_find_catch2()
endif()

add_subdirectory(core)

if(nom_build_apps)
    add_subdirectory(apps)
endif()

if(nom_build_tests)
    add_subdirectory(tests)
endif()
