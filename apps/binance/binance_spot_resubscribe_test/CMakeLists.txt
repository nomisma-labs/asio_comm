# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0

nom_app(binance_spot_resubscribe_test
    IMPORT_STD
    SOURCES main.cpp config.h ../../common_app_nodes.h
    DEPS Boost::headers nom::asio_comm nom::assembly nom::spd_logger)
