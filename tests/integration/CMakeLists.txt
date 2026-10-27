# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

# Integration checks for library usage and installation (docs/kit/howto.md#library-usage-integration-tests).

# template-only:begin
nom_library_usage_tests(library_usage)
# The app build must collect its source config into the common build/config tree.
if(TARGET hello)
    add_test(NAME ${PROJECT_NAME}_collected_config
        COMMAND ${CMAKE_COMMAND} -E compare_files
            ${PROJECT_SOURCE_DIR}/apps/hello/config/settings.json
            ${CMAKE_BINARY_DIR}/config/hello/settings.json)
    set_tests_properties(${PROJECT_NAME}_collected_config PROPERTIES LABELS integration)
    if(nom_install)
        add_test(NAME ${PROJECT_NAME}_installed_config
            COMMAND ${CMAKE_COMMAND} -E compare_files
                ${CMAKE_BINARY_DIR}/config/hello/settings.json
                ${PROJECT_BINARY_DIR}/tests/integration/library_usage/install/config/hello/settings.json)
        set_tests_properties(${PROJECT_NAME}_installed_config PROPERTIES
            FIXTURES_REQUIRED ${PROJECT_NAME}_installed LABELS integration)
    endif()
endif()
# The Boost example writes its settings gzip-compressed, then reads them back.
if(TARGET boost_example)
    set(boost_example_settings ${CMAKE_CURRENT_BINARY_DIR}/boost_example/settings.json.gz)
    add_test(NAME ${PROJECT_NAME}_boost_example_write
        COMMAND boost_example --name lab --repeat 2 --gzip ${boost_example_settings})
    add_test(NAME ${PROJECT_NAME}_boost_example_read
        COMMAND boost_example --settings ${boost_example_settings})
    set_tests_properties(${PROJECT_NAME}_boost_example_write PROPERTIES
        FIXTURES_SETUP ${PROJECT_NAME}_boost_settings LABELS integration)
    set_tests_properties(${PROJECT_NAME}_boost_example_read PROPERTIES
        FIXTURES_REQUIRED ${PROJECT_NAME}_boost_settings LABELS integration
        PASS_REGULAR_EXPRESSION "hello, lab 2")
endif()
# template-only:end
