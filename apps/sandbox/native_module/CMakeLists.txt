# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary
#
# Lab item 1: a native module (no headers) with a partition.

nom_lib(${PROJECT_NAME}_sandbox_geometry
    MODULES geometry.cppm point.cppm
    BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# Build the experiment's support library only when its app is requested.
set_target_properties(${PROJECT_NAME}_sandbox_geometry PROPERTIES EXCLUDE_FROM_ALL TRUE)

nom_app(sandbox_native_module SOURCES main.cpp DEPS ${PROJECT_NAME}_sandbox_geometry BUILD_FOR complete)
