# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

nom_app(https_example IMPORT_STD SOURCES main.cpp config.h ../common_app_nodes.h DEPS Boost::headers nom::asio_comm nom::spd_logger)
