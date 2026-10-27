# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

include(FetchContent)

# nom_require_submodule(<path>)
#
# Fails early, with the fix, when a submodule was not checked out: a plain `git clone` leaves
# external/ empty. Only checked for a standalone build; in a superbuild the outer project
# supplies the dependency.
function(nom_require_submodule path)
    if(NOT PROJECT_IS_TOP_LEVEL)
        return()
    endif()
    file(GLOB entries ${PROJECT_SOURCE_DIR}/${path}/*)
    if(NOT entries)
        message(FATAL_ERROR "${path} is empty. Run: git submodule update --init --recursive")
    endif()
endfunction()
