# Connectivity test

- Started (UTC): 2026-11-04T22:24:15Z
- Build directory: `/home/alex/work/nomisma-labs/asio_comm/build/dev`
- Build requested: true
- Feeder/provider runtime after connection: 1s / 10s
- Delay between applications: 10s
- Total process deadline: 60s, then up to 5s for termination
- Elapsed time uses monotonic uptime; UTC timestamps are informational.

Payload bytes exclude logging and transport overhead. Feeder counts cover relayed application
payloads, possibly including acknowledgements/heartbeats; they do not prove market-data readiness.
Binary/compressed payloads use their received size. Provider bytes describe the completed catalog.
An empty one-second sample is NO_DATA, not proof of a broken connection.

Build succeeded; see [build.log](build.log).

Discovered **20 feeders** and **19 providers**. Only existing binaries are tested.

| Application | Result | Exit | Elapsed (s) | Messages | Payload bytes | Details | Logs |
| --- | --- | ---: | ---: | ---: | ---: | --- | --- |
| binance_perp_feeder | PASS | 0 | 1.240 | 191 | 54083 | Connected and received application payload | [out](binance_perp_feeder.stdout.log), [err](binance_perp_feeder.stderr.log) |
| binance_perp_symbol_provider | PASS | 0 | 0.330 | 1 | 1172011 | Catalog written successfully | [out](binance_perp_symbol_provider.stdout.log), [err](binance_perp_symbol_provider.stderr.log) |
| binance_spot_feeder | PASS | 0 | 1.230 | 28 | 6466 | Connected and received application payload | [out](binance_spot_feeder.stdout.log), [err](binance_spot_feeder.stderr.log) |
| binance_spot_sbe_feeder | SKIP | — | 0.000 | — | — | BINANCE_SBE_API_KEY is not set | [out](binance_spot_sbe_feeder.stdout.log), [err](binance_spot_sbe_feeder.stderr.log) |
| binance_spot_symbol_provider | PASS | 0 | 6.130 | 1 | 6710185 | Catalog written successfully | [out](binance_spot_symbol_provider.stdout.log), [err](binance_spot_symbol_provider.stderr.log) |
| bitflyer_spot_feeder | PASS | 0 | 1.250 | 8 | 1328 | Connected and received application payload | [out](bitflyer_spot_feeder.stdout.log), [err](bitflyer_spot_feeder.stderr.log) |
| bitflyer_spot_symbol_provider | PASS | 0 | 0.140 | 1 | 435 | Catalog written successfully | [out](bitflyer_spot_symbol_provider.stdout.log), [err](bitflyer_spot_symbol_provider.stderr.log) |
| bitget_spot_feeder | PASS | 0 | 1.440 | 28 | 62486 | Connected and received application payload | [out](bitget_spot_feeder.stdout.log), [err](bitget_spot_feeder.stderr.log) |
| bitget_spot_symbol_provider | PASS | 0 | 0.410 | 1 | 2131786 | Catalog written successfully | [out](bitget_spot_symbol_provider.stdout.log), [err](bitget_spot_symbol_provider.stderr.log) |
| bybit_spot_feeder | PASS | 0 | 1.040 | 27 | 8506 | Connected and received application payload | [out](bybit_spot_feeder.stdout.log), [err](bybit_spot_feeder.stderr.log) |
| bybit_spot_symbol_provider | PASS | 0 | 0.060 | 1 | 282653 | Catalog written successfully | [out](bybit_spot_symbol_provider.stdout.log), [err](bybit_spot_symbol_provider.stderr.log) |
| coinbase_spot_feeder | PASS | 0 | 2.010 | 3 | 996 | Connected and received application payload | [out](coinbase_spot_feeder.stdout.log), [err](coinbase_spot_feeder.stderr.log) |
| coinbase_spot_symbol_provider | PASS | 0 | 2.580 | 1 | 749359 | Catalog written successfully | [out](coinbase_spot_symbol_provider.stdout.log), [err](coinbase_spot_symbol_provider.stderr.log) |
| crypto_com_spot_feeder | NO_DATA | 0 | 1.370 | 0 | 0 | Connected, but no application payload within the sample | [out](crypto_com_spot_feeder.stdout.log), [err](crypto_com_spot_feeder.stderr.log) |
| crypto_com_spot_symbol_provider | PASS | 0 | 0.180 | 1 | 545504 | Catalog written successfully | [out](crypto_com_spot_symbol_provider.stdout.log), [err](crypto_com_spot_symbol_provider.stderr.log) |
| deribit_perp_feeder | PASS | 0 | 1.540 | 7 | 47689 | Connected and received application payload | [out](deribit_perp_feeder.stdout.log), [err](deribit_perp_feeder.stderr.log) |
| deribit_perp_symbol_provider | PASS | 0 | 1.210 | 1 | 188626 | Catalog written successfully | [out](deribit_perp_symbol_provider.stdout.log), [err](deribit_perp_symbol_provider.stderr.log) |
| gate_spot_feeder | PASS | 0 | 1.310 | 8 | 6229 | Connected and received application payload | [out](gate_spot_feeder.stdout.log), [err](gate_spot_feeder.stderr.log) |
| gate_spot_symbol_provider | PASS | 0 | 0.760 | 1 | 1120586 | Catalog written successfully | [out](gate_spot_symbol_provider.stdout.log), [err](gate_spot_symbol_provider.stderr.log) |
| htx_spot_feeder | PASS | 0 | 1.240 | 6 | 4825 | Connected and received application payload | [out](htx_spot_feeder.stdout.log), [err](htx_spot_feeder.stderr.log) |
| htx_spot_symbol_provider | PASS | 0 | 0.450 | 1 | 1008787 | Catalog written successfully | [out](htx_spot_symbol_provider.stdout.log), [err](htx_spot_symbol_provider.stderr.log) |
| hyperliquid_perp_feeder | PASS | 0 | 1.170 | 15 | 13636 | Connected and received application payload | [out](hyperliquid_perp_feeder.stdout.log), [err](hyperliquid_perp_feeder.stderr.log) |
| hyperliquid_perp_symbol_provider | PASS | 0 | 0.330 | 1 | 17838 | Catalog written successfully | [out](hyperliquid_perp_symbol_provider.stdout.log), [err](hyperliquid_perp_symbol_provider.stderr.log) |
| hyperliquid_spot_feeder | PASS | 0 | 1.170 | 11 | 12220 | Connected and received application payload | [out](hyperliquid_spot_feeder.stdout.log), [err](hyperliquid_spot_feeder.stderr.log) |
| hyperliquid_spot_symbol_provider | PASS | 0 | 0.470 | 1 | 141306 | Catalog written successfully | [out](hyperliquid_spot_symbol_provider.stdout.log), [err](hyperliquid_spot_symbol_provider.stderr.log) |
| kraken_spot_feeder | PASS | 0 | 1.290 | 10 | 16841 | Connected and received application payload | [out](kraken_spot_feeder.stdout.log), [err](kraken_spot_feeder.stderr.log) |
| kraken_spot_symbol_provider | PASS | 0 | 0.460 | 1 | 580165 | Catalog written successfully | [out](kraken_spot_symbol_provider.stdout.log), [err](kraken_spot_symbol_provider.stderr.log) |
| kucoin_spot_feeder | PASS | 0 | 1.440 | 8 | 7854 | Connected and received application payload | [out](kucoin_spot_feeder.stdout.log), [err](kucoin_spot_feeder.stderr.log) |
| kucoin_spot_symbol_provider | PASS | 0 | 0.240 | 1 | 700087 | Catalog written successfully | [out](kucoin_spot_symbol_provider.stdout.log), [err](kucoin_spot_symbol_provider.stderr.log) |
| lbank_spot_feeder | PASS | 0 | 1.520 | 6 | 1418 | Connected and received application payload | [out](lbank_spot_feeder.stdout.log), [err](lbank_spot_feeder.stderr.log) |
| lbank_spot_symbol_provider | PASS | 0 | 1.420 | 1 | 147701 | Catalog written successfully | [out](lbank_spot_symbol_provider.stdout.log), [err](lbank_spot_symbol_provider.stderr.log) |
| mexc_spot_feeder | PASS | 0 | 1.490 | 20 | 8489 | Connected and received application payload | [out](mexc_spot_feeder.stdout.log), [err](mexc_spot_feeder.stderr.log) |
| mexc_spot_symbol_provider | PASS | 0 | 0.150 | 1 | 1490861 | Catalog written successfully | [out](mexc_spot_symbol_provider.stdout.log), [err](mexc_spot_symbol_provider.stderr.log) |
| okex_spot_feeder | PASS | 0 | 1.300 | 21 | 35693 | Connected and received application payload | [out](okex_spot_feeder.stdout.log), [err](okex_spot_feeder.stderr.log) |
| okx_spot_symbol_provider | PASS | 0 | 0.230 | 1 | 1307840 | Catalog written successfully | [out](okx_spot_symbol_provider.stdout.log), [err](okx_spot_symbol_provider.stderr.log) |
| upbit_spot_feeder | PASS | 0 | 1.270 | 3 | 4079 | Connected and received application payload | [out](upbit_spot_feeder.stdout.log), [err](upbit_spot_feeder.stderr.log) |
| upbit_spot_symbol_provider | PASS | 0 | 0.190 | 1 | 842 | Catalog written successfully | [out](upbit_spot_symbol_provider.stdout.log), [err](upbit_spot_symbol_provider.stderr.log) |
| xt_spot_feeder | PASS | 0 | 1.050 | 3 | 1311 | Connected and received application payload | [out](xt_spot_feeder.stdout.log), [err](xt_spot_feeder.stderr.log) |
| xt_spot_symbol_provider | PASS | 0 | 0.190 | 1 | 1173675 | Catalog written successfully | [out](xt_spot_symbol_provider.stdout.log), [err](xt_spot_symbol_provider.stderr.log) |

**Results:** 37 PASS, 0 FAIL, 1 NO_DATA, 0 TIMEOUT, 1 SKIP; 39/39 rows completed.

Finished (UTC): 2026-11-04T22:31:07Z. Total elapsed: 411.800s.
