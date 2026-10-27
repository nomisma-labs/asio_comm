# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

nom_c2test(tests
    SOURCES
        counter_test.cxx
        join_test.cxx
        settings_test.cxx
    DEPS nom::example)
