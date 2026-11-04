# XT.com Spot symbol provider

`xt_spot_symbol_provider` fetches an initial catalog with `nom::https` (HTTP/1.1 over verified TLS)
and Assembly/relay. No credentials are required.

## Build and run

From the repository root:

```sh
cmake --preset dev
cmake --build --preset dev --target xt_spot_symbol_provider
./build/dev/bin/xt_spot_symbol_provider > symbols.json
./build/dev/bin/xt_spot_symbol_provider --exit-after-sec 10 > symbols.json
./build/dev/bin/xt_spot_symbol_provider --help
```

Default URL: `https://sapi.xt.com/v4/public/symbol`. `--endpoint` overrides the complete URL, including the
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

Omit symbol and symbols to fetch all pairs. Omit version too: a matching cached version suppresses the symbol list. Preserve lowercase symbols and filters; consumers inspect state and openapiEnabled before using a pair.

10 requests/s/IP for single- and multiple-symbol queries. One request is made per run; there is no polling, batching, retry or shared
rate limiter. HTTP 429 and other non-200 responses fail without automatic retry.
See the [official API reference](https://doc.xt.com/docs/spot/Market/GetSymbolInformation).

Abbreviated entry (illustrative, not current trading rules):

```json
{"symbol":"btc_usdt","state":"ONLINE","baseCurrency":"btc","quoteCurrency":"usdt"}
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
`xt_spot_client`
owns HTTPS and its callbacks, and `symbol_output` consumes borrowed data synchronously through
the relay. HTTPS owns connection/request deadlines; SIGINT/SIGTERM close the request silently.
Success exits 0 only after the catalog is written; errors, cancellation and output failure exit 1.

The `asio_comm_https_symbol_providers` integration suite uses local TLS peers to check requests,
response assembly, API/HTTP errors, size limits and shutdown. Live verification is recorded in the
[applications overview](../../README.md#implementation-and-validation).
