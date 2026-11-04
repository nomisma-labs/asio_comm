# LBank Spot symbol provider

`lbank_spot_symbol_provider` fetches an initial catalog with `nom::https` (HTTP/1.1 over verified TLS)
and Assembly/relay. No credentials are required.

## Build and run

From the repository root:

```sh
cmake --preset dev
cmake --build --preset dev --target lbank_spot_symbol_provider
./build/dev/bin/lbank_spot_symbol_provider > symbols.json
./build/dev/bin/lbank_spot_symbol_provider --exit-after-sec 10 > symbols.json
./build/dev/bin/lbank_spot_symbol_provider --help
```

Default URL: `https://api.lbank.info/v2/accuracy.do`. `--endpoint` overrides the complete URL, including the
path/query, for another supported region or a local TLS peer. `--max-response-size BYTES`
sets the catalog body limit (default **16 MiB**); this is a cap, not an initial allocation.
The normal `dev` build skips this app; other presets include it.

## Runtime and shutdown

`--exit-after-sec SECONDS` has the same timing as the feeders: omitted means no additional
application limit, positive values count from connection (TLS for HTTPS; WebSocket handshake for
WS), and `0` closes immediately after connection before requesting a catalog. Existing transport
connection/request deadlines still apply; this limit excludes connection setup.

Ctrl+C (`SIGINT`) and `SIGTERM` stop during setup or retrieval. A provider exits **0** only after
writing a complete catalog. Timeout, cancellation (including `--exit-after-sec 0`), connection/API
errors and output failure exit **1**. Stops close the transport, cancel the exit timer and signal
wait, drain pending callbacks and flush the logger. Stop diagnostics go to stderr. Timer and signal
handlers share the I/O thread, so synchronous output can delay their execution.

The shared `app_terminator` precedes the output/client nodes. Clients relay `connected`/`failed`;
`symbol_output` relays `completed` after writing the catalog. The provider config sets
`require_completion = true`, preserving the one-shot success contract with the shared stop logic.
The `asio_comm_provider_exit` local TLS suite covers these paths for every provider.

## API decisions

accuracy.do without symbol returns specifications for all pairs; currencyPairs.do returns names only. Preserve lowercase symbols, priceAccuracy, quantityAccuracy and minimum amounts. The success result may be boolean true or the string "true".

The documented non-order request budget is 200 requests per 10 seconds. One request is made per run; there is no polling, batching, retry or shared
rate limiter. HTTP 429 and other non-200 responses fail without automatic retry.
See the [official API reference](https://www.lbank.com/docs/index.html#trading-pairs).

Abbreviated entry (illustrative, not current trading rules):

```json
{"symbol":"btc_usdt","priceAccuracy":"2","quantityAccuracy":"5"}
```

Example codes: `btc_usdt`, `eth_usdt`, `sol_usdt`.

## Workflow and integration

Connect → GET → check HTTP status and catalog envelope → relay the raw response → close.
The complete response envelope is preserved on stdout; application diagnostics use stderr.
Only basic string checks identify success/error fields; there is no JSON parser, DOM,
normalization or instrument filtering. Requests ask for `Accept-Encoding: identity`.
Redirects, compressed responses and authentication are not handled by this example.

[config.h](config.h) owns defaults, argument parsing and nonthrowing `app_config::validate()`.
The shared I/O node, terminator and lifecycle events are defined in [common_app_nodes.h](../../common_app_nodes.h).

In [main.cpp](main.cpp), `asio_service` owns the I/O context, and `app_terminator` owns signals, the optional exit timer and the exit result.
`lbank_spot_client`
owns HTTPS and its callbacks, and `symbol_output` consumes borrowed data synchronously through
the relay. HTTPS owns connection/request deadlines; SIGINT/SIGTERM close the request silently.
Success exits 0 only after the catalog is written; errors, cancellation and output failure exit 1.

The `asio_comm_https_symbol_providers` integration suite uses local TLS peers to check requests,
response assembly, API/HTTP errors, size limits and shutdown. Live verification is recorded in the
[applications overview](../../README.md#implementation-and-validation).
