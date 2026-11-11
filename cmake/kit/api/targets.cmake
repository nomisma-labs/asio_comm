# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: MIT

# nom_lib(<name> [STATIC | SHARED] [IMPORT_STD] [SOURCES <files>] [HEADERS <files>] [MODULES <files>] [BASE_DIR <dir>]
#                 [PUBLIC_DEPS <targets>] [PRIVATE_DEPS <targets>] [COMPILE_OPTIONS <options>])
#
# Creates <name> and its alias nom::<name>. Nomisma library APIs are declared in MODULES.
# Without SOURCES or MODULES it is an INTERFACE target for macros or dependency adapters.
# STATIC/SHARED selects compiled linkage explicitly; otherwise
# BUILD_SHARED_LIBS supplies the default. HEADERS and MODULES become file sets rooted at BASE_DIR.
# BASE_DIR defaults to <project>/src. Installed paths remain <name>/file.h.
# COMPILE_OPTIONS are PRIVATE for compiled libraries and INTERFACE for header-only targets.
# IMPORT_STD enables standard-library modules and requires a compiled library.
function(nom_lib name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "STATIC;SHARED;IMPORT_STD" "BASE_DIR" "SOURCES;HEADERS;MODULES;PUBLIC_DEPS;PRIVATE_DEPS;COMPILE_OPTIONS")
    if(arg_STATIC AND arg_SHARED)
        message(FATAL_ERROR "nom_lib(${name}): STATIC and SHARED are mutually exclusive")
    endif()
    if((arg_STATIC OR arg_SHARED) AND NOT (arg_SOURCES OR arg_MODULES))
        message(FATAL_ERROR "nom_lib(${name}): STATIC/SHARED requires SOURCES or MODULES")
    endif()
    if(arg_IMPORT_STD AND NOT (arg_SOURCES OR arg_MODULES))
        message(FATAL_ERROR "nom_lib(${name}): IMPORT_STD requires SOURCES or MODULES")
    endif()
    set(linkage)
    if(arg_STATIC)
        set(linkage STATIC)
    elseif(arg_SHARED)
        set(linkage SHARED)
    endif()
    if(NOT arg_BASE_DIR)
        set(arg_BASE_DIR "${PROJECT_SOURCE_DIR}/src")
    endif()

    if(arg_SOURCES OR arg_MODULES)
        add_library(${name} ${linkage})
        set(scope PUBLIC)
    else()
        add_library(${name} INTERFACE)
        set(scope INTERFACE)
    endif()
    add_library(nom::${name} ALIAS ${name})
    if(arg_IMPORT_STD)
        nom_enable_std_modules(${name})
    endif()

    if(arg_SOURCES)
        target_sources(${name} PRIVATE ${arg_SOURCES})
    endif()
    if(arg_HEADERS)
        target_sources(${name} ${scope} FILE_SET HEADERS BASE_DIRS ${arg_BASE_DIR} FILES ${arg_HEADERS})
    endif()
    if(arg_MODULES)
        target_sources(${name} PUBLIC FILE_SET CXX_MODULES BASE_DIRS ${arg_BASE_DIR} FILES ${arg_MODULES})
    endif()

    target_compile_features(${name} ${scope} cxx_std_23)
    if(arg_PUBLIC_DEPS)
        target_link_libraries(${name} ${scope} ${arg_PUBLIC_DEPS})
    endif()
    if(scope STREQUAL "PUBLIC")
        if(arg_COMPILE_OPTIONS)
            target_compile_options(${name} PRIVATE ${arg_COMPILE_OPTIONS})
        endif()
        if(arg_PRIVATE_DEPS)
            target_link_libraries(${name} PRIVATE ${arg_PRIVATE_DEPS})
        endif()
        set_target_properties(${name} PROPERTIES CXX_EXTENSIONS OFF)
        nom_set_warnings(${name})
        get_target_property(target_type ${name} TYPE)
        if(target_type STREQUAL "SHARED_LIBRARY" OR target_type STREQUAL "MODULE_LIBRARY")
            nom_set_install_library_path(${name} "${CMAKE_INSTALL_FULL_LIBDIR}")
        endif()
    elseif(arg_COMPILE_OPTIONS)
        target_compile_options(${name} INTERFACE ${arg_COMPILE_OPTIONS})
    endif()
endfunction()

# nom_app(<name> [IMPORT_STD] SOURCES <files> [DEPS <targets>] [BUILD_FOR <names>] [COMPILE_OPTIONS <options>])
#
# Creates target <name>; callers choose names unique across the whole build. The binary is
# bin/<name> in the project's build directory. If the app directory contains config/,
# collect it at <top-level build>/config/<app directory> on every build. When
# installation is enabled, install selected apps and their collected config. BUILD_FOR defaults
# to any; NOM_BUILD_PRESET=complete selects every app. Other apps remain explicitly buildable.
# COMPILE_OPTIONS apply only to this executable. Does nothing when nom_build_apps is OFF.
# IMPORT_STD enables standard-library modules only when the target is created.
function(nom_app name)
    if(NOT nom_build_apps)
        return()
    endif()
    cmake_parse_arguments(PARSE_ARGV 1 arg "IMPORT_STD" "" "SOURCES;DEPS;BUILD_FOR;COMPILE_OPTIONS")
    if(NOT arg_BUILD_FOR)
        set(arg_BUILD_FOR any)
    endif()
    set(selected FALSE)
    if(NOM_BUILD_PRESET STREQUAL "complete" OR "any" IN_LIST arg_BUILD_FOR OR NOM_BUILD_PRESET IN_LIST arg_BUILD_FOR)
        set(selected TRUE)
    endif()
    add_executable(${name} ${arg_SOURCES})
    if(arg_IMPORT_STD)
        nom_enable_std_modules(${name})
    endif()
    if(NOT selected)
        set_target_properties(${name} PROPERTIES EXCLUDE_FROM_ALL TRUE)
    endif()
    target_link_libraries(${name} PRIVATE ${arg_DEPS})
    target_compile_features(${name} PRIVATE cxx_std_23)
    if(arg_COMPILE_OPTIONS)
        target_compile_options(${name} PRIVATE ${arg_COMPILE_OPTIONS})
    endif()
    set_target_properties(${name} PROPERTIES
        OUTPUT_NAME ${name}
        RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin
        CXX_EXTENSIONS OFF)
    nom_set_warnings(${name})
    nom_set_install_library_path(${name} "${CMAKE_INSTALL_FULL_BINDIR}")

    if(nom_install AND selected)
        install(TARGETS ${name} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()

    set(config_source "${CMAKE_CURRENT_SOURCE_DIR}/config")
    if(IS_DIRECTORY "${config_source}")
        cmake_path(GET CMAKE_CURRENT_SOURCE_DIR FILENAME app_folder)
        set(config_target "${PROJECT_NAME}_copy_${app_folder}_config")
        set(config_destination "${CMAKE_BINARY_DIR}/config/${app_folder}")
        if(NOT TARGET ${config_target})
            add_custom_target(${config_target}
                COMMAND ${CMAKE_COMMAND} -E rm -rf "${config_destination}"
                COMMAND ${CMAKE_COMMAND} -E copy_directory "${config_source}" "${config_destination}"
                COMMENT "Collecting config for ${app_folder}"
                VERBATIM)
        endif()
        # The first app in a shared directory may be excluded while a later one is selected.
        get_property(config_install_registered TARGET ${config_target} PROPERTY NOM_CONFIG_INSTALL_REGISTERED)
        if(nom_install AND selected AND NOT config_install_registered)
            install(DIRECTORY "${config_destination}/" DESTINATION "config/${app_folder}")
            set_property(TARGET ${config_target} PROPERTY NOM_CONFIG_INSTALL_REGISTERED TRUE)
        endif()
        add_dependencies(${name} ${config_target})
    endif()
endfunction()
