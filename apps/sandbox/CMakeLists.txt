# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0
#
# Experiments: every subdirectory with a CMakeLists.txt is registered, so adding or deleting
# one needs no change here. tools/kit/add_sandbox_app.py adds apps with BUILD_FOR complete.

file(GLOB experiments LIST_DIRECTORIES true CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*)
foreach(experiment IN LISTS experiments)
    if(EXISTS ${experiment}/CMakeLists.txt)
        add_subdirectory(${experiment})
    endif()
endforeach()
