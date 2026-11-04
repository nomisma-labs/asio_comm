# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

nom_c2test(https IMPORT_STD SOURCES https_test.cxx cleanup_exception_test.cxx DEPS Boost::headers nom::asio_comm)
