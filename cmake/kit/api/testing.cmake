# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

# nom_c2test(<suite> [IMPORT_STD] SOURCES <files> [DEPS <targets>] [LABELS <labels>]
#            [PROPERTIES <name> <value>...])
#
# One Catch2 executable per test folder: target <project>_<suite>, registered with CTest and
# labeled unit; LABELS replaces that label list. PROPERTIES sets other test properties, such as
# TIMEOUT or RESOURCE_LOCK, so callers never spell the test name. Call nom_find_catch2() first
# when enabled. Does nothing when nom_build_tests is OFF.
# IMPORT_STD enables standard-library modules only when the target is created.
function(nom_c2test suite)
    if(NOT nom_build_tests)
        return()
    endif()
    cmake_parse_arguments(PARSE_ARGV 1 arg "IMPORT_STD" "" "SOURCES;DEPS;LABELS;PROPERTIES")
    if(NOT arg_LABELS)
        set(arg_LABELS unit)
    endif()
    set(target ${PROJECT_NAME}_${suite})
    add_executable(${target} ${arg_SOURCES})
    if(arg_IMPORT_STD)
        nom_enable_std_modules(${target})
    endif()
    target_link_libraries(${target} PRIVATE ${arg_DEPS} Catch2::Catch2WithMain)
    target_compile_features(${target} PRIVATE cxx_std_23)
    set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)
    nom_set_warnings(${target})
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES LABELS "${arg_LABELS}" ${arg_PROPERTIES})
endfunction()

# nom_library_usage_tests(<dir> [OPTIONS <-Dname=value>...])
#
# Integration tests that build and run <dir>, a separate project using this library:
# once through find_package against an installation in the build tree, and once from source
# through FetchContent. The test project receives LIBRARY_USAGE_MODE (package
# or source), LIBRARY_USAGE_SOURCE_DIR, this build's compiler, build type and flags, and OPTIONS.
# Does nothing when nom_build_tests is OFF; the installed-package checks also require nom_install.
function(nom_library_usage_tests dir)
    if(NOT nom_build_tests)
        return()
    endif()
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "OPTIONS")
    cmake_path(ABSOLUTE_PATH dir BASE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} OUTPUT_VARIABLE source_dir)
    set(work_dir ${PROJECT_BINARY_DIR}/tests/integration/library_usage)
    set(options
        -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        "-DCMAKE_CXX_FLAGS=${CMAKE_CXX_FLAGS}"
        ${arg_OPTIONS})

    if(nom_install)
        add_test(NAME ${PROJECT_NAME}_library_usage_install
            COMMAND ${CMAKE_COMMAND} --install ${PROJECT_BINARY_DIR} --prefix ${work_dir}/install)
        set_tests_properties(${PROJECT_NAME}_library_usage_install PROPERTIES
            FIXTURES_SETUP ${PROJECT_NAME}_installed LABELS integration)

        add_test(NAME ${PROJECT_NAME}_library_usage_package
            COMMAND ${CMAKE_CTEST_COMMAND}
                --build-and-test ${source_dir} ${work_dir}/package
                --build-generator ${CMAKE_GENERATOR}
                --build-options ${options} -DLIBRARY_USAGE_MODE=package -DCMAKE_PREFIX_PATH=${work_dir}/install
                --test-command ${CMAKE_CTEST_COMMAND} --output-on-failure)
        set_tests_properties(${PROJECT_NAME}_library_usage_package PROPERTIES
            FIXTURES_REQUIRED ${PROJECT_NAME}_installed LABELS integration)
    endif()

    add_test(NAME ${PROJECT_NAME}_library_usage_source
        COMMAND ${CMAKE_CTEST_COMMAND}
            --build-and-test ${source_dir} ${work_dir}/source
            --build-generator ${CMAKE_GENERATOR}
            --build-options ${options} -DLIBRARY_USAGE_MODE=source -DLIBRARY_USAGE_SOURCE_DIR=${PROJECT_SOURCE_DIR}
            --test-command ${CMAKE_CTEST_COMMAND} --output-on-failure)
    set_tests_properties(${PROJECT_NAME}_library_usage_source PROPERTIES LABELS integration)
endfunction()
