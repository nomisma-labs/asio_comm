# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

add_test(NAME ${PROJECT_NAME}_project_options
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/check_test.py
        --cmake ${CMAKE_COMMAND}
        --ctest ${CMAKE_CTEST_COMMAND}
        --compiler ${CMAKE_CXX_COMPILER}
        --generator "${CMAKE_GENERATOR}"
        --kit ${PROJECT_SOURCE_DIR}/cmake/kit)
set_tests_properties(${PROJECT_NAME}_project_options PROPERTIES LABELS kit TIMEOUT 120)
