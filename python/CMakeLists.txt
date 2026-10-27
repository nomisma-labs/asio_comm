# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

nom_python_module(cmake_template_python
    MODULE_NAME _native
    PACKAGE nomisma_example
    SOURCES native.cpp
    DEPS nom::example
    RUNTIME_LIBRARIES nom::example)
