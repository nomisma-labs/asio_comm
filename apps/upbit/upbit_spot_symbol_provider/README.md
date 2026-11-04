# Upbit Spot (Singapore) symbol provider

`upbit_spot_symbol_provider` fetches an initial catalog with `nom::https` (HTTP/1.1 over verified TLS)
and Assembly/relay. No credentials are required.

## Build and run

From the repository root:

```sh
cmake --preset dev
cmake --build --preset dev --target upbit_spot_symbol_provider
./build/dev/bin/upbit_spot_symbol_provider > symbols.json
./build/dev/bin/upbit_spot_symbol_provider --help
```

Default URL: `https://sg-api.upbit.com/v1/market/all?is_details=true`. `--endpoint` overrides the complete URL, including the
path/query, for another supported region or a local TLS peer. `--max-response-size BYTES`
sets the catalog body limit (default **16 MiB**); this is a cap, not an initial allocation.
The normal `dev` build skips this app; other presets include it.

## API decisions

Use the Singapore regional catalog to match the feeder. is_details=true includes market warnings. Names/codes are discovery metadata, not a complete set of tick, lot and order-size rules.

The quotation market group is limited to 10 requests/s/IP; do not send an Origin header.
See the [rate-limit rules](https://global-docs.upbit.com/reference/rate-limits).
One request is made per run; there is no polling, batching, retry or shared
rate limiter. HTTP 429 and other non-200 responses fail without automatic retry.
See the [official API reference](https://global-docs.upbit.com/reference/list-trading-pairs).

Abbreviated entry (illustrative, not current trading rules):

```json
{"market":"SGD-BTC","english_name":"Bitcoin","market_warning":"NONE"}
```

Example codes: `SGD-BTC`, `SGD-ETH`, `SGD-SOL`.

## Workflow and integration

Connect → GET → check HTTP status and catalog envelope → relay the raw response → close.
The complete response envelope is preserved on stdout; application diagnostics use stderr.
Only basic string checks identify success/error fields; there is no JSON parser, DOM,
normalization or instrument filtering. Requests ask for `Accept-Encoding: identity`.
Redirects, compressed responses and authentication are not handled by this example.

[config.h](config.h) owns defaults, argument parsing and nonthrowing `app_config::validate()`.
In [main.cpp](main.cpp), `asio_service` owns the I/O context/signals, `upbit_spot_client`
owns HTTPS and its callbacks, and `symbol_output` consumes borrowed data synchronously through
the relay. HTTPS owns connection/request deadlines; SIGINT/SIGTERM close the request silently.
Success exits 0 only after the catalog is written; errors, cancellation and output failure exit 1.

The `asio_comm_https_symbol_providers` integration suite uses local TLS peers to check requests,
response assembly, API/HTTP errors, size limits and shutdown. Live verification is recorded in the
[applications overview](../../README.md#implementation-and-validation).
