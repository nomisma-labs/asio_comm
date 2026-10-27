# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary
#
# A separate project that uses example the way another repo would. nom_library_usage_tests()
# in tests/integration/CMakeLists.txt builds it in both modes; it is not part of the main build.

cmake_minimum_required(VERSION 3.30)
project(example_library_usage LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(LIBRARY_USAGE_MODE "package" CACHE STRING "package: find_package(cmake_template); source: FetchContent")

if(LIBRARY_USAGE_MODE STREQUAL "package")
    find_package(cmake_template CONFIG REQUIRED)
else()
    include(FetchContent)
    FetchContent_Declare(cmake_template SOURCE_DIR ${LIBRARY_USAGE_SOURCE_DIR})
    FetchContent_MakeAvailable(cmake_template)
endif()

enable_testing()

add_executable(use_library use_library_test.cxx)
target_link_libraries(use_library PRIVATE nom::example)
add_test(NAME use_library COMMAND use_library)
