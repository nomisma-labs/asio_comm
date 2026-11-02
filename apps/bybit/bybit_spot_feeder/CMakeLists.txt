# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

nom_app(bybit_spot_feeder
    IMPORT_STD SOURCES main.cpp config.h
    DEPS Boost::headers nom::asio_comm nom::assembly nom::spd_logger)
