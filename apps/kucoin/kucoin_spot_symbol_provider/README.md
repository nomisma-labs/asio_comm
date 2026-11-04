# KuCoin Spot symbol provider

`kucoin_spot_symbol_provider` fetches an initial catalog with `nom::https` (HTTP/1.1 over verified TLS)
and Assembly/relay. No credentials are required.

## Build and run

From the repository root:

```sh
cmake --preset dev
cmake --build --preset dev --target kucoin_spot_symbol_provider
./build/dev/bin/kucoin_spot_symbol_provider > symbols.json
./build/dev/bin/kucoin_spot_symbol_provider --help
```

Default URL: `https://api.kucoin.com/api/v2/symbols`. `--endpoint` overrides the complete URL, including the
path/query, for another supported region or a local TLS peer. `--max-response-size BYTES`
sets the catalog body limit (default **16 MiB**); this is a cap, not an initial allocation.
The normal `dev` build skips this app; other presets include it.

## API decisions

GET /api/v2/symbols without market returns all Spot symbols. No token bootstrap or authentication is needed. Hyphenated symbol names and priceIncrement/baseIncrement/minFunds remain in the original response.

Request weight 4 in the public REST resource pool; share its budget across processes. See KuCoin rate-limit rules. One request is made per run; there is no polling, batching, retry or shared
rate limiter. HTTP 429 and other non-200 responses fail without automatic retry.
See the [official API reference](https://www.kucoin.com/en-au/docs-new/rest/spot-trading/market-data/get-all-symbols).

Abbreviated entry (illustrative, not current trading rules):

```json
{"symbol":"BTC-USDT","baseCurrency":"BTC","quoteCurrency":"USDT","enableTrading":true}
```

Example codes: `BTC-USDT`, `ETH-USDT`, `SOL-USDT`.

## Workflow and integration

Connect → GET → check HTTP status and catalog envelope → relay the raw response → close.
The complete response envelope is preserved on stdout; application diagnostics use stderr.
Only basic string checks identify success/error fields; there is no JSON parser, DOM,
normalization or instrument filtering. Requests ask for `Accept-Encoding: identity`.
Redirects, compressed responses and authentication are not handled by this example.

[config.h](config.h) owns defaults, argument parsing and nonthrowing `app_config::validate()`.
In [main.cpp](main.cpp), `asio_service` owns the I/O context/signals, `kucoin_spot_client`
owns HTTPS and its callbacks, and `symbol_output` consumes borrowed data synchronously through
the relay. HTTPS owns connection/request deadlines; SIGINT/SIGTERM close the request silently.
Success exits 0 only after the catalog is written; errors, cancellation and output failure exit 1.

The `asio_comm_https_symbol_providers` integration suite uses local TLS peers to check requests,
response assembly, API/HTTP errors, size limits and shutdown. Live verification is recorded in the
[applications overview](../../README.md#implementation-and-validation).
