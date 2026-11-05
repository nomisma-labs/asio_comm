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

Build succeeded. This committed snapshot omits links to local logs. The original build log,
per-app stdout/stderr and generated summary remain under
`build/dev/conn_test_results/20261104T222415Z-QiAy9N/` on the machine that ran the check;
those files are not included in the repository.

Discovered **20 feeders** and **19 providers**. Only existing binaries are tested.

| Application | Result | Exit | Elapsed (s) | Messages | Payload bytes | Details |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| binance_perp_feeder | PASS | 0 | 1.240 | 191 | 54083 | Connected and received application payload |
| binance_perp_symbol_provider | PASS | 0 | 0.330 | 1 | 1172011 | Catalog written successfully |
| binance_spot_feeder | PASS | 0 | 1.230 | 28 | 6466 | Connected and received application payload |
| binance_spot_sbe_feeder | SKIP | — | 0.000 | — | — | BINANCE_SBE_API_KEY is not set |
| binance_spot_symbol_provider | PASS | 0 | 6.130 | 1 | 6710185 | Catalog written successfully |
| bitflyer_spot_feeder | PASS | 0 | 1.250 | 8 | 1328 | Connected and received application payload |
| bitflyer_spot_symbol_provider | PASS | 0 | 0.140 | 1 | 435 | Catalog written successfully |
| bitget_spot_feeder | PASS | 0 | 1.440 | 28 | 62486 | Connected and received application payload |
| bitget_spot_symbol_provider | PASS | 0 | 0.410 | 1 | 2131786 | Catalog written successfully |
| bybit_spot_feeder | PASS | 0 | 1.040 | 27 | 8506 | Connected and received application payload |
| bybit_spot_symbol_provider | PASS | 0 | 0.060 | 1 | 282653 | Catalog written successfully |
| coinbase_spot_feeder | PASS | 0 | 2.010 | 3 | 996 | Connected and received application payload |
| coinbase_spot_symbol_provider | PASS | 0 | 2.580 | 1 | 749359 | Catalog written successfully |
| crypto_com_spot_feeder | NO_DATA | 0 | 1.370 | 0 | 0 | Connected, but no application payload within the sample |
| crypto_com_spot_symbol_provider | PASS | 0 | 0.180 | 1 | 545504 | Catalog written successfully |
| deribit_perp_feeder | PASS | 0 | 1.540 | 7 | 47689 | Connected and received application payload |
| deribit_perp_symbol_provider | PASS | 0 | 1.210 | 1 | 188626 | Catalog written successfully |
| gate_spot_feeder | PASS | 0 | 1.310 | 8 | 6229 | Connected and received application payload |
| gate_spot_symbol_provider | PASS | 0 | 0.760 | 1 | 1120586 | Catalog written successfully |
| htx_spot_feeder | PASS | 0 | 1.240 | 6 | 4825 | Connected and received application payload |
| htx_spot_symbol_provider | PASS | 0 | 0.450 | 1 | 1008787 | Catalog written successfully |
| hyperliquid_perp_feeder | PASS | 0 | 1.170 | 15 | 13636 | Connected and received application payload |
| hyperliquid_perp_symbol_provider | PASS | 0 | 0.330 | 1 | 17838 | Catalog written successfully |
| hyperliquid_spot_feeder | PASS | 0 | 1.170 | 11 | 12220 | Connected and received application payload |
| hyperliquid_spot_symbol_provider | PASS | 0 | 0.470 | 1 | 141306 | Catalog written successfully |
| kraken_spot_feeder | PASS | 0 | 1.290 | 10 | 16841 | Connected and received application payload |
| kraken_spot_symbol_provider | PASS | 0 | 0.460 | 1 | 580165 | Catalog written successfully |
| kucoin_spot_feeder | PASS | 0 | 1.440 | 8 | 7854 | Connected and received application payload |
| kucoin_spot_symbol_provider | PASS | 0 | 0.240 | 1 | 700087 | Catalog written successfully |
| lbank_spot_feeder | PASS | 0 | 1.520 | 6 | 1418 | Connected and received application payload |
| lbank_spot_symbol_provider | PASS | 0 | 1.420 | 1 | 147701 | Catalog written successfully |
| mexc_spot_feeder | PASS | 0 | 1.490 | 20 | 8489 | Connected and received application payload |
| mexc_spot_symbol_provider | PASS | 0 | 0.150 | 1 | 1490861 | Catalog written successfully |
| okex_spot_feeder | PASS | 0 | 1.300 | 21 | 35693 | Connected and received application payload |
| okx_spot_symbol_provider | PASS | 0 | 0.230 | 1 | 1307840 | Catalog written successfully |
| upbit_spot_feeder | PASS | 0 | 1.270 | 3 | 4079 | Connected and received application payload |
| upbit_spot_symbol_provider | PASS | 0 | 0.190 | 1 | 842 | Catalog written successfully |
| xt_spot_feeder | PASS | 0 | 1.050 | 3 | 1311 | Connected and received application payload |
| xt_spot_symbol_provider | PASS | 0 | 0.190 | 1 | 1173675 | Catalog written successfully |

**Results:** 37 PASS, 0 FAIL, 1 NO_DATA, 0 TIMEOUT, 1 SKIP; 39/39 rows completed.

Finished (UTC): 2026-11-04T22:31:07Z. Total elapsed: 411.800s.
