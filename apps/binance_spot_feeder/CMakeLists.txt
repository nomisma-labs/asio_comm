# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

nom_app(binance_spot_feeder
    IMPORT_STD
    SOURCES main.cpp
    DEPS nom::asio_comm nom::spd_logger)
