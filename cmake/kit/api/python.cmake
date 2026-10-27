# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

# nom_python_module(<target> MODULE_NAME <name> PACKAGE <dotted-name>
#                   SOURCES <files> [DEPS <targets>] [RUNTIME_LIBRARIES <shared-targets>])
#
# Builds a CPython extension and installs it with explicitly selected shared libraries into
# the package directory, under component nom_python only. Python files are selected by the
# backend's wheel.packages declaration. This initial fixture supports native Linux builds.
# RUNTIME_LIBRARIES is the complete non-system shared-library closure, including imported
# targets. It is not an automatic system-library collector or a manylinux repair step.
# Does nothing when nom_build_python is OFF; nom_python installation is independent of nom_install.
function(nom_python_module target)
    if(NOT nom_build_python)
        return()
    endif()
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "MODULE_NAME;PACKAGE" "SOURCES;DEPS;RUNTIME_LIBRARIES")
    if(arg_UNPARSED_ARGUMENTS OR arg_KEYWORDS_MISSING_VALUES OR NOT arg_SOURCES
       OR NOT arg_MODULE_NAME MATCHES "^[A-Za-z_][A-Za-z_0-9]*$"
       OR NOT arg_PACKAGE MATCHES "^[A-Za-z_][A-Za-z_0-9]*(\\.[A-Za-z_][A-Za-z_0-9]*)*$")
        message(FATAL_ERROR "nom_python_module(${target}): specify MODULE_NAME, PACKAGE and SOURCES")
    endif()
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_CROSSCOMPILING)
        message(FATAL_ERROR "nom_python_module currently supports native Linux builds only")
    endif()
    find_package(Python 3.10 REQUIRED COMPONENTS Interpreter Development.Module)
    if(NOT Python_INTERPRETER_ID STREQUAL "Python")
        message(FATAL_ERROR "nom_python_module currently supports CPython only")
    endif()
    string(REPLACE "." "/" package_directory "${arg_PACKAGE}")

    set(runtime_targets)
    foreach(runtime IN LISTS arg_RUNTIME_LIBRARIES)
        _nom_python_target(${runtime} runtime)
        get_target_property(type ${runtime} TYPE)
        if(NOT type STREQUAL "SHARED_LIBRARY")
            message(FATAL_ERROR "RUNTIME_LIBRARIES requires shared library targets: ${runtime}")
        endif()
        list(APPEND runtime_targets ${runtime})
    endforeach()
    list(REMOVE_DUPLICATES runtime_targets)

    # Follow target dependencies to diagnose omitted runtime libraries and give static
    # objects linked into the extension PIC. Imported static objects must already be PIC.
    set(pending ${arg_DEPS} ${runtime_targets})
    set(visited)
    while(pending)
        list(POP_FRONT pending dependency)
        _nom_python_target(${dependency} dependency)
        if(dependency IN_LIST visited)
            continue()
        endif()
        list(APPEND visited ${dependency})
        get_target_property(type ${dependency} TYPE)
        get_target_property(imported ${dependency} IMPORTED)
        if(type STREQUAL "SHARED_LIBRARY" AND NOT dependency IN_LIST runtime_targets)
            message(FATAL_ERROR "Python dependency ${dependency} must be listed in RUNTIME_LIBRARIES")
        elseif(type STREQUAL "STATIC_LIBRARY" OR type STREQUAL "OBJECT_LIBRARY")
            if(imported)
                get_target_property(pic ${dependency} INTERFACE_POSITION_INDEPENDENT_CODE)
                if(NOT pic)
                    message(FATAL_ERROR "Imported Python dependency ${dependency} must declare "
                        "INTERFACE_POSITION_INDEPENDENT_CODE=ON and contain PIC objects")
                endif()
            else()
                set_property(TARGET ${dependency} PROPERTY POSITION_INDEPENDENT_CODE ON)
            endif()
        endif()
        foreach(property IN ITEMS LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
            get_target_property(dependencies ${dependency} ${property})
            if(NOT dependencies)
                continue()
            endif()
            foreach(next IN LISTS dependencies)
                if(next MATCHES "^\\$<(LINK_ONLY|BUILD_INTERFACE):([^<>]+)>$")
                    set(next "${CMAKE_MATCH_2}")
                elseif(next MATCHES "^\\$<INSTALL_INTERFACE:")
                    continue()
                elseif(next MATCHES "\\$<")
                    message(FATAL_ERROR "Python dependency ${dependency} uses unsupported conditional link "
                        "expression '${next}'; provide an explicit target dependency graph")
                endif()
                if(TARGET ${next})
                    list(APPEND pending ${next})
                elseif(NOT next MATCHES "^(-l)?(c|m|dl|pthread|rt|stdc\\+\\+|gcc_s|atomic)$"
                       AND NOT next STREQUAL "-pthread" AND NOT next MATCHES "^::@")
                    message(FATAL_ERROR "Python dependency ${dependency} links '${next}' without a CMake "
                        "target; model non-system libraries as targets and list shared ones in RUNTIME_LIBRARIES")
                endif()
            endforeach()
        endforeach()
    endwhile()

    Python_add_library(${target} MODULE WITH_SOABI ${arg_SOURCES})
    target_compile_features(${target} PRIVATE cxx_std_23)
    target_link_libraries(${target} PRIVATE ${arg_DEPS})
    if(runtime_targets)
        add_dependencies(${target} ${runtime_targets})
    endif()
    set_target_properties(${target} PROPERTIES OUTPUT_NAME "${arg_MODULE_NAME}"
        CXX_EXTENSIONS OFF INSTALL_RPATH "$ORIGIN" INSTALL_RPATH_USE_LINK_PATH OFF
        BUILD_RPATH_USE_ORIGIN ON)
    nom_set_warnings(${target})
    install(TARGETS ${target} LIBRARY DESTINATION "${package_directory}"
        COMPONENT nom_python EXCLUDE_FROM_ALL)

    foreach(runtime IN LISTS runtime_targets)
        get_target_property(imported ${runtime} IMPORTED)
        if(imported)
            _nom_python_check_imported(${runtime})
            install(IMPORTED_RUNTIME_ARTIFACTS ${runtime}
                LIBRARY DESTINATION "${package_directory}" COMPONENT nom_python EXCLUDE_FROM_ALL)
        else()
            set_target_properties(${runtime} PROPERTIES INSTALL_RPATH "$ORIGIN"
                INSTALL_RPATH_USE_LINK_PATH OFF BUILD_RPATH_USE_ORIGIN ON)
            install(TARGETS ${runtime} LIBRARY DESTINATION "${package_directory}"
                COMPONENT nom_python EXCLUDE_FROM_ALL NAMELINK_SKIP)
        endif()
    endforeach()
endfunction()
