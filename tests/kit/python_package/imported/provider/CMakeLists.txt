# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

cmake_minimum_required(VERSION 3.30)
project(wheel_provider VERSION 1.0.0 LANGUAGES CXX)

include(${NOM_KIT_PATH}/nom_kit.cmake)
nom_project_options()

# Model an independently installed C-compatible third-party library.
nom_lib(wheel_provider SHARED SOURCES provider.cpp)
install(TARGETS wheel_provider EXPORT wheel_provider_targets LIBRARY DESTINATION lib)
install(EXPORT wheel_provider_targets NAMESPACE installed::
    FILE wheel_provider-config.cmake DESTINATION lib/cmake/wheel_provider)
