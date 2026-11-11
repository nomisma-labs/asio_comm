# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0

nom_c2test(https IMPORT_STD SOURCES https_test.cxx cleanup_exception_test.cxx DEPS Boost::headers nom::asio_comm)
