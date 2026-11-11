# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0

# Integration checks for library usage and installation (docs/kit/howto.md#library-usage-integration-tests).

nom_library_usage_tests(library_usage)

if(nom_build_tests)
    find_package(Python 3.10 REQUIRED COMPONENTS Interpreter)
    add_test(NAME ${PROJECT_NAME}_conn_test_runner
        COMMAND ${Python_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/conn_test_runner_test.py
            --script ${PROJECT_SOURCE_DIR}/tools/asio_comm/conn_test.sh)
    set_tests_properties(${PROJECT_NAME}_conn_test_runner PROPERTIES LABELS integration TIMEOUT 60)
endif()

if(nom_build_tests AND TARGET web_socket_example)
    # A client driven by the local TLS peer; it is a test fixture, not an installed app.
    block(SCOPE_FOR VARIABLES)
        set(nom_install OFF)
        nom_app(asio_comm_headers_client IMPORT_STD SOURCES web_socket/headers_test.cxx
            DEPS Boost::headers nom::asio_comm nom::core)
        nom_app(asio_comm_disconnecting_client IMPORT_STD SOURCES web_socket/disconnecting_test.cxx
            DEPS Boost::headers nom::asio_comm nom::core)
        nom_app(asio_comm_move_client IMPORT_STD SOURCES web_socket/move_test.cxx
            DEPS Boost::headers nom::asio_comm nom::core)
        nom_app(asio_comm_passive_ping_client IMPORT_STD SOURCES web_socket/passive_ping_test.cxx
            DEPS Boost::headers nom::asio_comm nom::core)
        nom_app(asio_comm_active_ping_client IMPORT_STD SOURCES web_socket/active_ping_test.cxx
            DEPS Boost::headers nom::asio_comm nom::core)
        nom_app(asio_comm_large_messages_client IMPORT_STD SOURCES web_socket/large_messages_test.cxx
            DEPS Boost::headers nom::asio_comm nom::core)
        nom_app(asio_comm_connection_deadline_client IMPORT_STD SOURCES web_socket/connection_deadline_test.cxx
            DEPS Boost::headers nom::asio_comm nom::core ${CMAKE_DL_LIBS})
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
    add_test(NAME ${PROJECT_NAME}_passive_ping
        COMMAND ${Python_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/web_socket/passive_ping_test.py
            --client $<TARGET_FILE:asio_comm_passive_ping_client> --openssl ${openssl_executable})
    set_tests_properties(${PROJECT_NAME}_passive_ping PROPERTIES LABELS integration TIMEOUT 45)
    add_test(NAME ${PROJECT_NAME}_active_ping
        COMMAND ${Python_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/web_socket/active_ping_test.py
            --client $<TARGET_FILE:asio_comm_active_ping_client> --openssl ${openssl_executable})
    set_tests_properties(${PROJECT_NAME}_active_ping PROPERTIES LABELS integration TIMEOUT 60)
    add_test(NAME ${PROJECT_NAME}_connection_deadline
        COMMAND ${Python_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/web_socket/connection_deadline_test.py
            --client $<TARGET_FILE:asio_comm_connection_deadline_client> --openssl ${openssl_executable})
    set_tests_properties(${PROJECT_NAME}_connection_deadline PROPERTIES LABELS integration TIMEOUT 45)
endif()

if(nom_build_tests AND TARGET binance_spot_resubscribe_test)
    find_package(Python 3.10 REQUIRED COMPONENTS Interpreter)
    find_program(openssl_executable openssl REQUIRED)
    add_test(NAME ${PROJECT_NAME}_binance_spot_resubscribe
        COMMAND ${Python_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/web_socket/resubscribe_test.py
            --client $<TARGET_FILE:binance_spot_resubscribe_test> --openssl ${openssl_executable})
    set_tests_properties(${PROJECT_NAME}_binance_spot_resubscribe PROPERTIES LABELS integration TIMEOUT 30)
endif()

if(nom_build_tests)
    set(feeder_exit_args)
    foreach(feeder IN ITEMS
            binance_perp_feeder binance_spot_feeder binance_spot_sbe_feeder
            bitflyer_spot_feeder bitget_spot_feeder bybit_spot_feeder coinbase_spot_feeder
            crypto_com_spot_feeder deribit_perp_feeder gate_spot_feeder htx_spot_feeder
            hyperliquid_perp_feeder hyperliquid_spot_feeder kraken_spot_feeder kucoin_spot_feeder
            lbank_spot_feeder mexc_spot_feeder okex_spot_feeder upbit_spot_feeder xt_spot_feeder)
        if(TARGET ${feeder})
            list(APPEND feeder_exit_args --client $<TARGET_FILE:${feeder}>)
        endif()
    endforeach()
    if(feeder_exit_args)
        find_package(Python 3.10 REQUIRED COMPONENTS Interpreter)
        find_program(openssl_executable openssl REQUIRED)
        add_test(NAME ${PROJECT_NAME}_feeder_exit
            COMMAND ${Python_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/web_socket/feeder_exit_test.py
                ${feeder_exit_args} --openssl ${openssl_executable})
        set_tests_properties(${PROJECT_NAME}_feeder_exit PROPERTIES LABELS integration TIMEOUT 120)
    endif()
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
    set(provider_exit_args)
    foreach(provider IN ITEMS
            binance_perp_symbol_provider binance_spot_symbol_provider bitflyer_spot_symbol_provider
            bitget_spot_symbol_provider bybit_spot_symbol_provider coinbase_spot_symbol_provider
            crypto_com_spot_symbol_provider deribit_perp_symbol_provider gate_spot_symbol_provider
            htx_spot_symbol_provider hyperliquid_perp_symbol_provider hyperliquid_spot_symbol_provider
            kraken_spot_symbol_provider kucoin_spot_symbol_provider lbank_spot_symbol_provider
            mexc_spot_symbol_provider okx_spot_symbol_provider upbit_spot_symbol_provider xt_spot_symbol_provider)
        if(TARGET ${provider})
            list(APPEND provider_exit_args --client $<TARGET_FILE:${provider}>)
        endif()
    endforeach()
    if(provider_exit_args)
        find_package(Python 3.10 REQUIRED COMPONENTS Interpreter)
        find_program(openssl_executable openssl REQUIRED)
        add_test(NAME ${PROJECT_NAME}_provider_exit
            COMMAND ${Python_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/provider_exit_test.py
                ${provider_exit_args} --openssl ${openssl_executable})
        set_tests_properties(${PROJECT_NAME}_provider_exit PROPERTIES LABELS integration TIMEOUT 120)
    endif()
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

if(nom_build_tests AND TARGET https_example)
    block(SCOPE_FOR VARIABLES)
        set(nom_install OFF)
        nom_app(asio_comm_https_client IMPORT_STD SOURCES https/client_test.cxx
            DEPS Boost::headers nom::asio_comm nom::core)
        nom_app(asio_comm_https_move_client IMPORT_STD SOURCES https/move_test.cxx
            DEPS Boost::headers nom::asio_comm nom::core)
    endblock()
    find_package(Python 3.10 REQUIRED COMPONENTS Interpreter)
    find_program(openssl_executable openssl REQUIRED)
    add_test(NAME ${PROJECT_NAME}_https_loopback
        COMMAND ${Python_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/https/loopback_test.py
            --client $<TARGET_FILE:asio_comm_https_client> --example $<TARGET_FILE:https_example>
            --move-client $<TARGET_FILE:asio_comm_https_move_client>
            --openssl ${openssl_executable})
    set_tests_properties(${PROJECT_NAME}_https_loopback PROPERTIES LABELS integration TIMEOUT 60)
endif()

if(nom_build_tests)
    set(https_symbol_provider_args)
    foreach(app_target IN ITEMS
        binance_perp_symbol_provider
        upbit_spot_symbol_provider
        okx_spot_symbol_provider
        bybit_spot_symbol_provider
        bitget_spot_symbol_provider
        gate_spot_symbol_provider
        kucoin_spot_symbol_provider
        mexc_spot_symbol_provider
        htx_spot_symbol_provider
        crypto_com_spot_symbol_provider
        lbank_spot_symbol_provider
        xt_spot_symbol_provider
        bitflyer_spot_symbol_provider
    )
        if(TARGET ${app_target})
            list(APPEND https_symbol_provider_args --client $<TARGET_FILE:${app_target}>)
        endif()
    endforeach()
    if(https_symbol_provider_args)
        find_package(Python 3.10 REQUIRED COMPONENTS Interpreter)
        find_program(openssl_executable openssl REQUIRED)
        add_test(NAME ${PROJECT_NAME}_https_symbol_providers
            COMMAND ${Python_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/https/symbol_providers_test.py
                --openssl ${openssl_executable} ${https_symbol_provider_args})
        set_tests_properties(${PROJECT_NAME}_https_symbol_providers PROPERTIES LABELS integration TIMEOUT 90)
    endif()
endif()
