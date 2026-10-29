# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

nom_c2test(web_socket IMPORT_STD SOURCES web_socket_test.cxx connection_exception_test.cxx
    DEPS nom::asio_comm nom::spd_logger)
