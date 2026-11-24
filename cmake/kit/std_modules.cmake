# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: MIT

# Include before the first project() enabling CXX, including in source/package consumers.
# CMake 4.4 requires this experimental gate. CMake 4.5 supports import std through scanning.
if(CMAKE_VERSION VERSION_GREATER_EQUAL 4.4 AND CMAKE_VERSION VERSION_LESS 4.5)
    set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "f35a9ac6-8463-4d38-8eec-5d6008153e7d")
endif()

# nom_enable_std_modules(<target>)
# Opt in each compiled target importing std or consuming a module that imports std.
# Prefer IMPORT_STD in kit target helpers; use this directly for existing external targets.
function(nom_enable_std_modules target)
    get_target_property(target_type ${target} TYPE)
    if(target_type STREQUAL "INTERFACE_LIBRARY")
        message(FATAL_ERROR "nom_enable_std_modules requires a compiled target: ${target}")
    endif()
    if(CMAKE_VERSION VERSION_LESS 4.5)
        if(NOT 23 IN_LIST CMAKE_CXX_COMPILER_IMPORT_STD)
            message(FATAL_ERROR "CMake 4.4 requires C++23 import std support. Include "
                "cmake/kit/std_modules.cmake before the first project() enabling CXX and "
                "configure with cmake --fresh (docs/kit/build.md#modules).")
        endif()
        set_property(TARGET ${target} PROPERTY CXX_MODULE_STD ON)
    else()
        if(NOT EXISTS "${CMAKE_CXX_STDLIB_MODULES_JSON}")
            message(FATAL_ERROR "C++ standard-library module metadata is required for ${target}. "
                "Use Ninja with GCC 16 or Clang 22 and installed standard-library module sources. "
                "${CMAKE_CXX_COMPILER_IMPORT_STD_ERROR_MESSAGE}")
        endif()
        set_property(TARGET ${target} PROPERTY CXX_SCAN_FOR_MODULES ON)
    endif()
    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        return()
    endif()

    # Clang warns about the vendor's intentionally reserved std module names.
    # Restrict that exception to vendor sources, including their synthetic BMIs.
    file(READ "${CMAKE_CXX_STDLIB_MODULES_JSON}" std_metadata)
    cmake_path(GET CMAKE_CXX_STDLIB_MODULES_JSON PARENT_PATH std_metadata_dir)
    string(JSON std_module_count LENGTH "${std_metadata}" modules)
    math(EXPR std_module_last "${std_module_count} - 1")
    foreach(index RANGE ${std_module_last})
        string(JSON std_module_name GET "${std_metadata}" modules ${index} logical-name)
        if(NOT std_module_name STREQUAL "std" AND NOT std_module_name STREQUAL "std.compat")
            continue()
        endif()
        string(JSON std_source GET "${std_metadata}" modules ${index} source-path)
        cmake_path(ABSOLUTE_PATH std_source BASE_DIRECTORY "${std_metadata_dir}" NORMALIZE)
        get_property(std_source_options SOURCE "${std_source}" TARGET_DIRECTORY ${target} PROPERTY COMPILE_OPTIONS)
        if(NOT "-Wno-reserved-module-identifier" IN_LIST std_source_options)
            set_property(SOURCE "${std_source}" TARGET_DIRECTORY ${target} APPEND
                PROPERTY COMPILE_OPTIONS -Wno-reserved-module-identifier)
        endif()
    endforeach()
endfunction()
