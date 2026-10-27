# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

# Integration checks for library usage and installation (docs/kit/howto.md#library-usage-integration-tests).

if(nom_build_tests AND TARGET asio_comm_example)
    set(example_config "${CMAKE_BINARY_DIR}/config/asio_comm_example/asio_comm_example.json")

    add_test(NAME ${PROJECT_NAME}_example_workflow
        COMMAND asio_comm_example --config "${example_config}")
    set_tests_properties(${PROJECT_NAME}_example_workflow PROPERTIES
        LABELS integration
        PASS_REGULAR_EXPRESSION "asio_comm_example finished")

    add_test(NAME ${PROJECT_NAME}_example_check_config
        COMMAND asio_comm_example --config "${example_config}" --check-config)
    set_tests_properties(${PROJECT_NAME}_example_check_config PROPERTIES
        LABELS integration
        PASS_REGULAR_EXPRESSION "Config OK for asio_comm_example"
        FAIL_REGULAR_EXPRESSION "app started with config|Example warning|finished")
endif()
