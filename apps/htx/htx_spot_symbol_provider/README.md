# HTX Spot symbol provider

`htx_spot_symbol_provider` fetches an initial catalog with `nom::https` (HTTP/1.1 over verified TLS)
and Assembly/relay. No credentials are required.

## Build and run

From the repository root:

```sh
cmake --preset dev
cmake --build --preset dev --target htx_spot_symbol_provider
./build/dev/bin/htx_spot_symbol_provider > symbols.json
./build/dev/bin/htx_spot_symbol_provider --help
```

Default URL: `https://api.huobi.pro/v2/settings/common/symbols`. `--endpoint` overrides the complete URL, including the
path/query, for another supported region or a local TLS peer. `--max-response-size BYTES`
sets the catalog body limit (default **16 MiB**); this is a cap, not an initial allocation.
The normal `dev` build skips this app; other presets include it.

## API decisions

Omit ts: supplying it requests incremental changes, not a full initial catalog. Require status=ok and full=1. Preserve lowercase sc codes, bc/qc currencies and precision fields, including offline entries.

One public request; consult the reference-data and API rate-limit rules before scheduling refreshes. One request is made per run; there is no polling, batching, retry or shared
rate limiter. HTTP 429 and other non-200 responses fail without automatic retry.
See the [official API reference](https://huobiapi.github.io/docs/spot/v1/en/#get-all-supported-trading-symbol-v2).

Abbreviated entry (illustrative, not current trading rules):

```json
{"sc":"btcusdt","bc":"btc","qc":"usdt","state":"online"}
```

Example codes: `btcusdt`, `ethusdt`, `solusdt`.

## Workflow and integration

Connect → GET → check HTTP status and catalog envelope → relay the raw response → close.
The complete response envelope is preserved on stdout; application diagnostics use stderr.
Only basic string checks identify success/error fields; there is no JSON parser, DOM,
normalization or instrument filtering. Requests ask for `Accept-Encoding: identity`.
Redirects, compressed responses and authentication are not handled by this example.

[config.h](config.h) owns defaults, argument parsing and nonthrowing `app_config::validate()`.
In [main.cpp](main.cpp), `asio_service` owns the I/O context/signals, `htx_spot_client`
owns HTTPS and its callbacks, and `symbol_output` consumes borrowed data synchronously through
the relay. HTTPS owns connection/request deadlines; SIGINT/SIGTERM close the request silently.
Success exits 0 only after the catalog is written; errors, cancellation and output failure exit 1.

The `asio_comm_https_symbol_providers` integration suite uses local TLS peers to check requests,
response assembly, API/HTTP errors, size limits and shutdown. Live verification is recorded in the
[applications overview](../../README.md#implementation-and-validation).
