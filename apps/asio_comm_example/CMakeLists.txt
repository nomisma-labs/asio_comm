# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

nom_app(asio_comm_example IMPORT_STD SOURCES main.cpp config.h
    DEPS nom::spd_logger nom::assembly)
