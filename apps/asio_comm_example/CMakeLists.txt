# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

nom_app(asio_comm_example SOURCES main.cpp
    DEPS nom::config_types nom::json_config nom::spd_logger nom::assembly)
