# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

nom_app(okex_spot_feeder
    IMPORT_STD
    SOURCES main.cpp
    DEPS Boost::headers nom::asio_comm nom::assembly nom::spd_logger)
