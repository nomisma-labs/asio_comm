# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

nom_lib(asio_comm IMPORT_STD MODULES web_socket.cppm SOURCES web_socket.cpp
    PUBLIC_DEPS nom::base_url nom::assembly Boost::headers OpenSSL::SSL Threads::Threads
    PRIVATE_DEPS nom::base_enum nom::spd_logger)

nom_install_package(TARGETS asio_comm DEPENDENCIES "base 0.3" assembly spd_logger Boost OpenSSL Threads)
