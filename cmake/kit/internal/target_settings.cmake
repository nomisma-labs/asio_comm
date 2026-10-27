# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

function(nom_set_warnings target)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

# Installed apps and shared libraries locate packaged libraries after prefix relocation.
function(nom_set_install_library_path target origin_directory)
    if(APPLE)
        set(origin "@loader_path")
    elseif(UNIX)
        set(origin "$ORIGIN")
    else()
        return()
    endif()
    file(RELATIVE_PATH relative_libdir "${origin_directory}" "${CMAKE_INSTALL_FULL_LIBDIR}")
    if(relative_libdir STREQUAL "")
        set(path "${origin}")
    else()
        set(path "${origin}/${relative_libdir}")
    endif()
    set_property(TARGET ${target} APPEND PROPERTY INSTALL_RPATH "${path}")
endfunction()
