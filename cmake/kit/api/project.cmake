# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: MIT

# nom_project_options()
#
# Call right after project(). Defines the project's standard options, named <PROJECT>_<NAME>
# (target and option names are global in a superbuild), and mirrors each into a variable of
# the calling directory, so the project's CMake files never spell out the project name:
#
#   option                   variable            default             controls
#   <PROJECT>_BUILD_TESTS    nom_build_tests     ON when top level   test targets and registration
#   <PROJECT>_BUILD_APPS     nom_build_apps      ON when top level   apps and config collection
#   <PROJECT>_BUILD_PYTHON   nom_build_python    OFF                 optional Python extensions
#   <PROJECT>_INSTALL        nom_install         ON                  native install rules and package config
function(nom_project_options)
    # Shared across composed repos; custom presets can inherit their parent's app selection.
    set(NOM_BUILD_PRESET dev CACHE STRING "App selection preset; complete includes every app")
    string(TOUPPER ${PROJECT_NAME} prefix)
    option(${prefix}_BUILD_TESTS "Build the tests" ${PROJECT_IS_TOP_LEVEL})
    option(${prefix}_BUILD_APPS "Build the apps, including the sandbox" ${PROJECT_IS_TOP_LEVEL})
    option(${prefix}_BUILD_PYTHON "Build Python extensions" OFF)
    option(${prefix}_INSTALL "Generate install rules" ON)
    foreach(name IN ITEMS BUILD_TESTS BUILD_APPS BUILD_PYTHON INSTALL)
        string(TOLOWER "nom_${name}" variable)
        set(${variable} ${${prefix}_${name}} PARENT_SCOPE)
    endforeach()
endfunction()
