# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

# Integration checks for library usage and installation (docs/kit/howto.md#library-usage-integration-tests).

nom_library_usage_tests(library_usage)

if(nom_build_tests AND TARGET web_socket_example)
    # A client driven by the local TLS peer; it is a test fixture, not an installed app.
    block(SCOPE_FOR VARIABLES)
        set(nom_install OFF)
        nom_app(asio_comm_headers_client IMPORT_STD SOURCES web_socket/headers_test.cxx
            DEPS Boost::headers nom::asio_comm nom::spd_logger)
        nom_app(asio_comm_disconnecting_client IMPORT_STD SOURCES web_socket/disconnecting_test.cxx
            DEPS Boost::headers nom::asio_comm nom::spd_logger)
        nom_app(asio_comm_move_client IMPORT_STD SOURCES web_socket/move_test.cxx
            DEPS Boost::headers nom::asio_comm nom::spd_logger)
        nom_app(asio_comm_large_messages_client IMPORT_STD SOURCES web_socket/large_messages_test.cxx
            DEPS Boost::headers nom::asio_comm nom::spd_logger)
    endblock()

    find_package(Python 3.10 REQUIRED COMPONENTS Interpreter)
    find_program(openssl_executable openssl REQUIRED)
    set(binance_feeder_args)
    if(TARGET binance_spot_feeder)
        list(APPEND binance_feeder_args --feeder-client $<TARGET_FILE:binance_spot_feeder>)
    endif()
    if(TARGET binance_perp_feeder)
        list(APPEND binance_feeder_args --perp-feeder-client $<TARGET_FILE:binance_perp_feeder>)
    endif()
    add_test(NAME ${PROJECT_NAME}_web_socket_loopback
        COMMAND ${Python_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/web_socket/loopback_test.py
            --client $<TARGET_FILE:web_socket_example>
            --headers-client $<TARGET_FILE:asio_comm_headers_client>
            --move-client $<TARGET_FILE:asio_comm_move_client>
            --disconnecting-client $<TARGET_FILE:asio_comm_disconnecting_client> --openssl ${openssl_executable}
            ${binance_feeder_args})
    set_tests_properties(${PROJECT_NAME}_web_socket_loopback PROPERTIES LABELS integration TIMEOUT 60)
endif()

if(nom_build_tests AND TARGET binance_spot_resubscribe_test)
    find_package(Python 3.10 REQUIRED COMPONENTS Interpreter)
    find_program(openssl_executable openssl REQUIRED)
    add_test(NAME ${PROJECT_NAME}_binance_spot_resubscribe
        COMMAND ${Python_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/web_socket/resubscribe_test.py
            --client $<TARGET_FILE:binance_spot_resubscribe_test> --openssl ${openssl_executable})
    set_tests_properties(${PROJECT_NAME}_binance_spot_resubscribe PROPERTIES LABELS integration TIMEOUT 30)
endif()

if(nom_build_tests AND TARGET binance_spot_symbol_provider AND TARGET asio_comm_large_messages_client)
    find_package(Python 3.10 REQUIRED COMPONENTS Interpreter)
    find_program(openssl_executable openssl REQUIRED)
    add_test(NAME ${PROJECT_NAME}_symbol_provider
        COMMAND ${Python_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/web_socket/symbol_provider_test.py
            --client $<TARGET_FILE:binance_spot_symbol_provider>
            --example-client $<TARGET_FILE:web_socket_example>
            --large-client $<TARGET_FILE:asio_comm_large_messages_client> --openssl ${openssl_executable})
    set_tests_properties(${PROJECT_NAME}_symbol_provider PROPERTIES LABELS integration TIMEOUT 90)
endif()

if(nom_build_tests)
    set(exchange_feeder_args)
    foreach(exchange IN ITEMS bitflyer deribit okex)
        if(exchange STREQUAL "deribit")
            set(feeder_target ${exchange}_perp_feeder)
        else()
            set(feeder_target ${exchange}_spot_feeder)
        endif()
        if(TARGET ${feeder_target})
            list(APPEND exchange_feeder_args --${exchange}-client $<TARGET_FILE:${feeder_target}>)
        endif()
    endforeach()
    if(exchange_feeder_args)
        find_package(Python 3.10 REQUIRED COMPONENTS Interpreter)
        find_program(openssl_executable openssl REQUIRED)
        add_test(NAME ${PROJECT_NAME}_exchange_feeders
            COMMAND ${Python_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/web_socket/feeders_test.py
                --openssl ${openssl_executable} ${exchange_feeder_args})
        set_tests_properties(${PROJECT_NAME}_exchange_feeders PROPERTIES LABELS integration TIMEOUT 60)
    endif()
endif()

if(nom_build_tests AND TARGET asio_comm_example)
    add_test(NAME ${PROJECT_NAME}_example_workflow
        COMMAND asio_comm_example)
    set_tests_properties(${PROJECT_NAME}_example_workflow PROPERTIES
        LABELS integration
        PASS_REGULAR_EXPRESSION "asio_comm_example finished")
endif()

if(nom_build_tests)
    set(exchange_app_args)
    foreach(app_target IN ITEMS
        binance_spot_sbe_feeder
        bitget_spot_feeder
        bybit_spot_feeder
        coinbase_spot_feeder
        coinbase_spot_symbol_provider
        crypto_com_spot_feeder
        deribit_perp_symbol_provider
        gate_spot_feeder
        htx_spot_feeder
        hyperliquid_perp_feeder
        hyperliquid_perp_symbol_provider
        hyperliquid_spot_feeder
        hyperliquid_spot_symbol_provider
        kraken_spot_feeder
        kraken_spot_symbol_provider
        kucoin_spot_feeder
        lbank_spot_feeder
        mexc_spot_feeder
        upbit_spot_feeder
        xt_spot_feeder
    )
        if(TARGET ${app_target})
            list(APPEND exchange_app_args --client $<TARGET_FILE:${app_target}>)
        endif()
    endforeach()
    if(exchange_app_args)
        find_package(Python 3.10 REQUIRED COMPONENTS Interpreter)
        find_program(openssl_executable openssl REQUIRED)
        add_test(NAME ${PROJECT_NAME}_exchange_apps
            COMMAND ${Python_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/web_socket/exchange_apps_test.py
                --openssl ${openssl_executable} ${exchange_app_args})
        set_tests_properties(${PROJECT_NAME}_exchange_apps PROPERTIES LABELS integration TIMEOUT 90)
    endif()
endif()
