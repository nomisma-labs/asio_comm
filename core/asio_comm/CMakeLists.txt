# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

nom_lib(asio_comm IMPORT_STD
    MODULES asio_comm.cppm web_socket.cppm ws_params.cppm events.cppm timer.cppm https.cppm http_validation.cppm
    SOURCES web_socket.cpp ws_params.cpp timer.cpp https.cpp
    PUBLIC_DEPS nom::base_url nom::assembly Boost::headers OpenSSL::SSL Threads::Threads
    PRIVATE_DEPS nom::base_enum nom::spd_logger Boost::url)

nom_install_package(TARGETS asio_comm DEPENDENCIES "base 0.3" assembly spd_logger "Boost 1.90 CONFIG COMPONENTS url" OpenSSL Threads)
