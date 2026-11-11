# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0

nom_lib(asio_comm IMPORT_STD
    MODULES asio_comm.cppm web_socket.cppm ws_params.cppm events.cppm timer.cppm https.cppm http_validation.cppm
    SOURCES web_socket.cpp ws_params.cpp timer.cpp https.cpp
    PUBLIC_DEPS nom::core Boost::headers OpenSSL::SSL Threads::Threads
    PRIVATE_DEPS Boost::url)

nom_install_package(TARGETS asio_comm DEPENDENCIES "nom_core 0.3" "Boost 1.90 CONFIG COMPONENTS url" OpenSSL Threads)
