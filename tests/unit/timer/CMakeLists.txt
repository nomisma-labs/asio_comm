# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

nom_c2test(timer IMPORT_STD SOURCES timer_test.cxx DEPS Boost::headers nom::asio_comm)
