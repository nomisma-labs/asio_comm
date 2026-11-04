# Bybit Spot symbol provider

`bybit_spot_symbol_provider` fetches an initial catalog with `nom::https` (HTTP/1.1 over verified TLS)
and Assembly/relay. No credentials are required.

## Build and run

From the repository root:

```sh
cmake --preset dev
cmake --build --preset dev --target bybit_spot_symbol_provider
./build/dev/bin/bybit_spot_symbol_provider > symbols.json
./build/dev/bin/bybit_spot_symbol_provider --help
```

Default URL: `https://api.bybit.com/v5/market/instruments-info?category=spot`. `--endpoint` overrides the complete URL, including the
path/query, for another supported region or a local TLS peer. `--max-response-size BYTES`
sets the catalog body limit (default **16 MiB**); this is a cap, not an initial allocation.
The normal `dev` build skips this app; other presets include it.

## API decisions

category=spot selects Spot and omitting symbol requests its catalog. Spot explicitly does not support pagination: do not supply limit or cursor. Preserve priceFilter and lotSizeFilter without normalizing them.

The IP-wide limits also cover other clients; see the
[rate-limit rules](https://bybit-exchange.github.io/docs/v5/rate-limit).
One request is made per run; there is no polling, batching, retry or shared
rate limiter. HTTP 429 and other non-200 responses fail without automatic retry.
See the [official API reference](https://bybit-exchange.github.io/docs/v5/market/instrument).

Abbreviated entry (illustrative, not current trading rules):

```json
{"symbol":"BTCUSDT","baseCoin":"BTC","quoteCoin":"USDT","status":"Trading"}
```

Example codes: `BTCUSDT`, `ETHUSDT`, `SOLUSDT`.

## Workflow and integration

Connect → GET → check HTTP status and catalog envelope → relay the raw response → close.
The complete response envelope is preserved on stdout; application diagnostics use stderr.
Only basic string checks identify success/error fields; there is no JSON parser, DOM,
normalization or instrument filtering. Requests ask for `Accept-Encoding: identity`.
Redirects, compressed responses and authentication are not handled by this example.

[config.h](config.h) owns defaults, argument parsing and nonthrowing `app_config::validate()`.
The shared I/O node and service commands are defined in [common_app_nodes.h](../../common_app_nodes.h).

In [main.cpp](main.cpp), `provider_state` extends the shared I/O context with signals and the completion result.
`bybit_spot_client`
owns HTTPS and its callbacks, and `symbol_output` consumes borrowed data synchronously through
the relay. HTTPS owns connection/request deadlines; SIGINT/SIGTERM close the request silently.
Success exits 0 only after the catalog is written; errors, cancellation and output failure exit 1.

The `asio_comm_https_symbol_providers` integration suite uses local TLS peers to check requests,
response assembly, API/HTTP errors, size limits and shutdown. Live verification is recorded in the
[applications overview](../../README.md#implementation-and-validation).
