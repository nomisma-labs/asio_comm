# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0

nom_app(deribit_perp_feeder
    BUILD_FOR complete release clang asan ci
    IMPORT_STD
    SOURCES main.cpp config.h ../../common_app_nodes.h
    DEPS Boost::headers nom::asio_comm nom::assembly nom::spd_logger)
