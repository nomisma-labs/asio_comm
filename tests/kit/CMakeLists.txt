# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

# Tests of the shared kit and tools themselves, labeled kit. Each one builds throwaway projects or
# repos of its own with this build's compiler; none uses this build's targets. New repos don't get
# them (tools/init_project.py).

find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
add_subdirectory(project_options)

option(CMAKE_TEMPLATE_TEST_PYTHON_PACKAGE "Build and test installed native Python wheels" OFF)
if(CMAKE_TEMPLATE_TEST_PYTHON_PACKAGE)
    if(NOT NOM_PYTHON_EXECUTABLE)
        message(FATAL_ERROR "Python wheel tests require NOM_PYTHON_EXECUTABLE. "
            "Create build/python-env with python3 -m venv, then install "
            "tools/kit/python_package_requirements.txt with that environment's pip.")
    endif()
    execute_process(COMMAND ${NOM_PYTHON_EXECUTABLE} -c "import build, pip, scikit_build_core"
        RESULT_VARIABLE python_package_dependencies OUTPUT_QUIET ERROR_QUIET)
    if(NOT python_package_dependencies EQUAL 0)
        message(FATAL_ERROR "NOM_PYTHON_EXECUTABLE must provide build, pip and scikit-build-core. "
            "Install tools/kit/python_package_requirements.txt with that environment's pip.")
    endif()
    find_program(python_package_ninja ninja REQUIRED)
    add_test(NAME ${PROJECT_NAME}_python_package
        COMMAND ${NOM_PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/python_package/check.py
            --source ${PROJECT_SOURCE_DIR} --python ${NOM_PYTHON_EXECUTABLE}
            --cmake ${CMAKE_COMMAND} --compiler ${CMAKE_CXX_COMPILER}
            --ninja ${python_package_ninja})
    set_tests_properties(${PROJECT_NAME}_python_package PROPERTIES
        LABELS "kit;python_package" TIMEOUT 300)
endif()

add_test(NAME ${PROJECT_NAME}_app_presets
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/app_presets/check.py
        --cmake ${CMAKE_COMMAND}
        --compiler ${CMAKE_CXX_COMPILER}
        --generator "${CMAKE_GENERATOR}"
        --kit ${PROJECT_SOURCE_DIR}/cmake/kit)
set_tests_properties(${PROJECT_NAME}_app_presets PROPERTIES LABELS kit)

add_test(NAME ${PROJECT_NAME}_compile_options
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/compile_options/check.py
        --cmake ${CMAKE_COMMAND}
        --compiler ${CMAKE_CXX_COMPILER}
        --generator "${CMAKE_GENERATOR}"
        --kit ${PROJECT_SOURCE_DIR}/cmake/kit)
set_tests_properties(${PROJECT_NAME}_compile_options PROPERTIES LABELS kit)

add_test(NAME ${PROJECT_NAME}_boost_package
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/boost_package/check.py
        --cmake ${CMAKE_COMMAND}
        --compiler ${CMAKE_CXX_COMPILER}
        --generator "${CMAKE_GENERATOR}"
        --kit ${PROJECT_SOURCE_DIR}/cmake/kit)
set_tests_properties(${PROJECT_NAME}_boost_package PROPERTIES LABELS kit)

add_test(NAME ${PROJECT_NAME}_test_selection
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_selection/check.py
        --cmake ${CMAKE_COMMAND}
        --ctest ${CMAKE_CTEST_COMMAND}
        --compiler ${CMAKE_CXX_COMPILER}
        --generator "${CMAKE_GENERATOR}"
        --kit ${PROJECT_SOURCE_DIR}/cmake/kit)
set_tests_properties(${PROJECT_NAME}_test_selection PROPERTIES LABELS kit)

add_test(NAME ${PROJECT_NAME}_dependency_updates
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/dependency_updates/check.py
        --source ${PROJECT_SOURCE_DIR})
set_tests_properties(${PROJECT_NAME}_dependency_updates PROPERTIES LABELS kit)

add_test(NAME ${PROJECT_NAME}_template_sync
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/template_sync/check.py
        --script ${PROJECT_SOURCE_DIR}/tools/kit/sync_template.py)
set_tests_properties(${PROJECT_NAME}_template_sync PROPERTIES LABELS kit)

if(UNIX)
    add_test(NAME ${PROJECT_NAME}_git_hooks
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/git_hooks/check.py
            --source ${PROJECT_SOURCE_DIR})
    set_tests_properties(${PROJECT_NAME}_git_hooks PROPERTIES LABELS kit)

    add_test(NAME ${PROJECT_NAME}_relocation
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/relocation/check.py
            --cmake ${CMAKE_COMMAND} --compiler ${CMAKE_CXX_COMPILER}
            --generator "${CMAKE_GENERATOR}" --kit ${PROJECT_SOURCE_DIR}/cmake/kit)
    set_tests_properties(${PROJECT_NAME}_relocation PROPERTIES LABELS kit)
endif()
