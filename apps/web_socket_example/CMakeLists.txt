# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0

nom_app(web_socket_example
    IMPORT_STD
    SOURCES main.cpp config.h
    DEPS Boost::headers nom::asio_comm nom::core)
