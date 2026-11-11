# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0

# General examples; exchange applications are grouped below without changing executable names.
add_subdirectory(asio_comm_example)
add_subdirectory(web_socket_example)
add_subdirectory(https_example)
add_subdirectory(timer_example)

add_subdirectory(binance)
add_subdirectory(bitflyer)
add_subdirectory(bitget)
add_subdirectory(bybit)
add_subdirectory(coinbase)
add_subdirectory(crypto_com)
add_subdirectory(deribit)
add_subdirectory(gate)
add_subdirectory(htx)
add_subdirectory(hyperliquid)
add_subdirectory(kraken)
add_subdirectory(kucoin)
add_subdirectory(lbank)
add_subdirectory(mexc)
add_subdirectory(okx)
add_subdirectory(upbit)
add_subdirectory(xt)

# Explicit group builds; omitting ALL preserves each app's default preset selection.
add_custom_target(feeders DEPENDS
    binance_perp_feeder
    binance_spot_feeder
    binance_spot_sbe_feeder
    bitflyer_spot_feeder
    bitget_spot_feeder
    bybit_spot_feeder
    coinbase_spot_feeder
    crypto_com_spot_feeder
    deribit_perp_feeder
    gate_spot_feeder
    htx_spot_feeder
    hyperliquid_perp_feeder
    hyperliquid_spot_feeder
    kraken_spot_feeder
    kucoin_spot_feeder
    lbank_spot_feeder
    mexc_spot_feeder
    okex_spot_feeder
    upbit_spot_feeder
    xt_spot_feeder)

add_custom_target(providers DEPENDS
    binance_perp_symbol_provider
    binance_spot_symbol_provider
    bitflyer_spot_symbol_provider
    bitget_spot_symbol_provider
    bybit_spot_symbol_provider
    coinbase_spot_symbol_provider
    crypto_com_spot_symbol_provider
    deribit_perp_symbol_provider
    gate_spot_symbol_provider
    htx_spot_symbol_provider
    hyperliquid_perp_symbol_provider
    hyperliquid_spot_symbol_provider
    kraken_spot_symbol_provider
    kucoin_spot_symbol_provider
    lbank_spot_symbol_provider
    mexc_spot_symbol_provider
    okx_spot_symbol_provider
    upbit_spot_symbol_provider
    xt_spot_symbol_provider)

add_subdirectory(sandbox)
