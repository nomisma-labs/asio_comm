# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: MIT

# nom_find_boost()
#
# The system Boost 1.90 or newer, with the compiled libraries every repo can link:
# Boost::program_options, Boost::filesystem and Boost::iostreams. The arguments are recorded
# for nom_install_package(), so a package that lists Boost in DEPENDENCIES finds the same.
macro(nom_find_boost)
    set(_nom_boost_args 1.90 CONFIG COMPONENTS program_options filesystem iostreams)
    find_package(Boost ${_nom_boost_args} REQUIRED)
    set_property(GLOBAL PROPERTY NOM_FIND_ARGS_Boost ${_nom_boost_args})
    unset(_nom_boost_args)
endmacro()
