# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

# Tests carrying a label listed in NOM_SKIP_TEST_LABELS stay out of every ctest run in this build
# tree, with or without a test preset; the dev preset leaves out the integration and kit tests.
# The kit lists them in the build tree's CTestCustom.cmake, which ctest reads before running tests.
set(NOM_SKIP_TEST_LABELS "" CACHE STRING "Labels of the tests that ctest leaves out in this build tree")

# Runs at the end of the top-level directory, when every test exists.
function(nom_write_test_selection)
    set(skipped "")
    set(directories "${CMAKE_SOURCE_DIR}")
    while(directories AND NOM_SKIP_TEST_LABELS)
        list(POP_FRONT directories directory)
        get_property(subdirectories DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
        list(APPEND directories ${subdirectories})
        get_property(tests DIRECTORY "${directory}" PROPERTY TESTS)
        foreach(test IN LISTS tests)
            get_test_property("${test}" LABELS DIRECTORY "${directory}" labels)
            foreach(label IN LISTS labels)
                if(label IN_LIST NOM_SKIP_TEST_LABELS)
                    list(APPEND skipped "${test}")
                    break()
                endif()
            endforeach()
        endforeach()
    endwhile()
    if(skipped)
        list(LENGTH skipped count)
        string(REPLACE ";" " or " labels "${NOM_SKIP_TEST_LABELS}")
        message(STATUS "ctest leaves out the ${count} tests labeled ${labels} in this build tree "
                       "(NOM_SKIP_TEST_LABELS)")
    endif()
    list(TRANSFORM skipped REPLACE "(.+)" "    \"\\1\"\n")
    list(JOIN skipped "" names)
    file(WRITE "${CMAKE_BINARY_DIR}/CTestCustom.cmake"
        "# Written by the Nomisma CMake kit: ctest leaves out these tests in this build tree, the\n"
        "# ones carrying a label in NOM_SKIP_TEST_LABELS (${NOM_SKIP_TEST_LABELS}).\n"
        "set(CTEST_CUSTOM_TESTS_IGNORE\n${names})\n")
endfunction()

cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL nom_write_test_selection)
