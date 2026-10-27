# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

cmake_minimum_required(VERSION 3.30)

# The version lives only here; releases are tagged v<version> (docs/kit/build.md#versioning).
project(cmake_template
    VERSION 0.11.1
    DESCRIPTION "Nomisma C++ repository template and build-system lab"
    LANGUAGES CXX)

include(cmake/kit/nom_kit.cmake)
nom_project_options()

# Dependencies come from external/ submodules (docs/kit/howto.md#a-dependency). In a superbuild the
# outermost FetchContent_Declare wins, so every repo shares one copy.

# template-only:begin
# nlohmann_json's own CMake project works as a subproject, so it is used as is.
nom_require_submodule(external/nlohmann_json)
set(JSON_Install ${nom_install}) # find_package consumers need nlohmann next to our package
FetchContent_Declare(nlohmann_json SOURCE_DIR ${PROJECT_SOURCE_DIR}/external/nlohmann_json SYSTEM)
FetchContent_MakeAvailable(nlohmann_json)
# template-only:end

# Boost comes from system packages (docs/kit/build.md#dependencies).
nom_find_boost()

# Discover Catch2 before any code directory can register an inline test suite.
if(nom_build_tests)
    enable_testing()
    nom_find_catch2()
endif()

add_subdirectory(core)

# template-only:begin
if(nom_build_python)
    add_subdirectory(python)
endif()
# template-only:end

if(nom_build_apps)
    add_subdirectory(apps)
endif()

if(nom_build_tests)
    add_subdirectory(tests)
endif()
