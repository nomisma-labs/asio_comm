# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: MIT

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# nom_install_package(TARGETS <targets> [DEPENDENCIES <packages>])
#
# Installs the targets with their header and module file sets, and generates
# <project>-config.cmake, so consumers can find_package(<project>) and link nom::<target>.
# DEPENDENCIES are the packages the public interface needs; the config finds them first, with
# the version and components a kit service found them with (NOM_FIND_ARGS_<package>, which
# nom_find_boost() records). Module interface sources are installed next to the headers;
# consumers compile their own BMIs, since BMIs are specific to the compiler and its flags.
# Does nothing when nom_install is OFF.
function(nom_install_package)
    if(NOT nom_install)
        return()
    endif()
    cmake_parse_arguments(PARSE_ARGV 0 arg "" "" "TARGETS;DEPENDENCIES")
    set(config_dir ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME})

    set(module_args "")
    foreach(target IN LISTS arg_TARGETS)
        get_target_property(module_sets ${target} CXX_MODULE_SETS)
        if(module_sets)
            set(module_args FILE_SET CXX_MODULES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
        endif()
    endforeach()

    install(TARGETS ${arg_TARGETS}
        EXPORT ${PROJECT_NAME}_targets
        FILE_SET HEADERS
        ${module_args})
    install(EXPORT ${PROJECT_NAME}_targets
        NAMESPACE nom::
        FILE ${PROJECT_NAME}-targets.cmake
        DESTINATION ${config_dir}
        CXX_MODULES_DIRECTORY modules)

    set(NOM_PACKAGE_DEPENDENCIES "")
    foreach(dependency IN LISTS arg_DEPENDENCIES)
        get_property(arguments GLOBAL PROPERTY NOM_FIND_ARGS_${dependency})
        list(PREPEND arguments ${dependency})
        list(JOIN arguments " " arguments)
        string(APPEND NOM_PACKAGE_DEPENDENCIES "find_dependency(${arguments})\n")
    endforeach()
    configure_package_config_file(${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../templates/package_config.cmake.in
        ${PROJECT_BINARY_DIR}/${PROJECT_NAME}-config.cmake
        INSTALL_DESTINATION ${config_dir})
    # SemVer: while MAJOR is 0 any release may break the API, so only the same MINOR is compatible.
    if(PROJECT_VERSION_MAJOR EQUAL 0)
        set(compatibility SameMinorVersion)
    else()
        set(compatibility SameMajorVersion)
    endif()
    write_basic_package_version_file(${PROJECT_BINARY_DIR}/${PROJECT_NAME}-config-version.cmake
        COMPATIBILITY ${compatibility})
    install(FILES
        ${PROJECT_BINARY_DIR}/${PROJECT_NAME}-config.cmake
        ${PROJECT_BINARY_DIR}/${PROJECT_NAME}-config-version.cmake
        DESTINATION ${config_dir})
    # The generated package config contains MIT-licensed kit code.
    install(FILES ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../LICENSE
        DESTINATION ${config_dir} RENAME LICENSE-MIT.txt)
endfunction()
