# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0

nom_c2test(timer IMPORT_STD SOURCES timer_test.cxx DEPS Boost::headers nom::asio_comm)
