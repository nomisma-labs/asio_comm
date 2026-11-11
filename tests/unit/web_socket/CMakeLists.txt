# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0

nom_c2test(web_socket IMPORT_STD SOURCES web_socket_test.cxx connection_exception_test.cxx params_test.cxx
    DEPS Boost::headers nom::asio_comm nom::core)
