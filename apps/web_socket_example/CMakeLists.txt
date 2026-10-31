# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

nom_app(web_socket_example
    IMPORT_STD
    SOURCES main.cpp
    DEPS Boost::headers nom::asio_comm nom::spd_logger)
