# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

nom_app(boost_example SOURCES main.cpp
    DEPS nom::example Boost::program_options Boost::filesystem Boost::iostreams)
