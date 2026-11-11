# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0

cmake_minimum_required(VERSION 4.4)
include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/kit/std_modules.cmake)
project(asio_comm_library_usage LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Boost 1.90 CONFIG REQUIRED)

if(LIBRARY_USAGE_MODE STREQUAL "package")
    find_package(asio_comm CONFIG REQUIRED)
else()
    include(FetchContent)
    FetchContent_Declare(asio_comm SOURCE_DIR ${LIBRARY_USAGE_SOURCE_DIR})
    FetchContent_MakeAvailable(asio_comm)
endif()

enable_testing()
add_executable(use_library use_library_test.cxx)
nom_enable_std_modules(use_library)
target_link_libraries(use_library PRIVATE Boost::headers nom::asio_comm nom::core)
add_test(NAME use_library COMMAND use_library)
