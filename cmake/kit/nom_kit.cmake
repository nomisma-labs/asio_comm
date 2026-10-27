# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary
#
# Nomisma CMake kit. Template-owned: change it in cmake_template, then pull the change into
# derived repos with tools/kit/sync_template.py. Reference: docs/kit/build.md.

set(_nom_kit_version 0.11.2)

# The same language flags everywhere: compiled module interfaces are flag-sensitive.
if(NOT DEFINED CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 23)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT DEFINED CMAKE_CXX_EXTENSIONS)
    set(CMAKE_CXX_EXTENSIONS OFF)
endif()

# In a superbuild every repo brings its own copy of the kit. Like FetchContent, the first
# (outermost) one wins; a different version is reported, since the copies should be in sync.
get_property(_nom_kit_loaded GLOBAL PROPERTY NOM_KIT_VERSION)
if(_nom_kit_loaded)
    if(NOT _nom_kit_loaded VERSION_EQUAL _nom_kit_version)
        message(WARNING "nom kit ${_nom_kit_version} (${CMAKE_CURRENT_LIST_DIR}) ignored: "
                        "${_nom_kit_loaded} is already loaded")
    endif()
    return()
endif()
set_property(GLOBAL PROPERTY NOM_KIT_VERSION ${_nom_kit_version})

# The pre-project entry point also supplies the per-target import std helper.
include(${CMAKE_CURRENT_LIST_DIR}/std_modules.cmake)

# Client-facing project, target, testing, installation and Python helpers.
include(${CMAKE_CURRENT_LIST_DIR}/api/project.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/api/targets.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/api/testing.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/api/install.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/api/python.cmake)

# Dependency acquisition and adapters.
include(${CMAKE_CURRENT_LIST_DIR}/deps/submodules.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/deps/catch2.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/deps/boost.cmake)

# Implementation utilities shared by the public helpers.
include(${CMAKE_CURRENT_LIST_DIR}/internal/target_settings.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/internal/python_runtime.cmake)

# The build tree's test selection: NOM_SKIP_TEST_LABELS.
include(${CMAKE_CURRENT_LIST_DIR}/internal/test_selection.cmake)
