# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

# The wheel demonstrates shipping a shared C++ library alongside its Python extension.
set(example_linkage)
if(nom_build_python)
    set(example_linkage SHARED)
endif()
nom_lib(example ${example_linkage}
    SOURCES
        counter.cpp
        settings.cpp
    HEADERS
        macros.h
    MODULES
        example.cppm
        counter.cppm
        join.cppm
        settings.cppm
        types.cppm
    PRIVATE_DEPS nlohmann_json::nlohmann_json)

nom_install_package(TARGETS example DEPENDENCIES nlohmann_json)
