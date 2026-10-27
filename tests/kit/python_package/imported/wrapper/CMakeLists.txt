# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

cmake_minimum_required(VERSION 3.30)
project(imported_wheel VERSION 1.0.0 LANGUAGES CXX)

include(${NOM_KIT_PATH}/nom_kit.cmake)
nom_project_options()
find_package(wheel_provider CONFIG REQUIRED)
set(runtime_libraries RUNTIME_LIBRARIES installed::wheel_provider)
if(WHEEL_TEST_OMIT_RUNTIME)
    set(runtime_libraries)
endif()
nom_python_module(imported_native MODULE_NAME _native PACKAGE nom_imported_example
    SOURCES native.cpp
    DEPS installed::wheel_provider
    ${runtime_libraries})
