# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0

cmake_minimum_required(VERSION 4.4)

include(cmake/kit/std_modules.cmake)

# The version lives only here; releases are tagged v<version> (docs/kit/build.md#versioning).
project(asio_comm
    VERSION 0.5.0
    DESCRIPTION "C++ communication protocols for cryptocurrency exchanges"
    LANGUAGES CXX)

include(cmake/kit/nom_kit.cmake)
nom_project_options()

if(nom_install)
    include(GNUInstallDirs)
    install(FILES ${PROJECT_SOURCE_DIR}/LICENSE
        DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/${PROJECT_NAME})
endif()

# nom_core selects the core component versions and makes their targets available.
# In a superbuild the outermost FetchContent_Declare still selects each shared repo.
nom_require_submodule(external/nom_core)
FetchContent_Declare(nom_core SOURCE_DIR ${PROJECT_SOURCE_DIR}/external/nom_core)
FetchContent_MakeAvailable(nom_core)

# Boost comes from system packages (docs/kit/build.md#dependencies).
nom_find_boost()
find_package(Boost 1.90 CONFIG REQUIRED COMPONENTS url)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

# Discover Catch2 before any code directory can register an inline test suite.
if(nom_build_tests)
    enable_testing()
    nom_find_catch2()
endif()

add_subdirectory(src)

if(nom_build_apps)
    add_subdirectory(apps)
endif()

if(nom_build_tests)
    add_subdirectory(tests)
endif()
