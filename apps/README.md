# Example applications

[Project overview and shared build instructions](../README.md)

Exchange applications live under `apps/<exchange>/<app>/` (for example, `apps/binance/binance_spot_feeder/`).
Executable and CMake target names are unchanged. General examples stay directly under `apps/`.

Feeders and symbol providers are included in the default builds for `complete`, `release`,
`clang`, `asan`, and `ci`. The `dev` default build skips them, including `./bootstrap.sh dev`,
but each target remains available explicitly:

```sh
cmake --preset dev
cmake --build --preset dev --target binance_spot_feeder binance_spot_symbol_provider
```

Group targets build all feeders or all symbol providers, including in `dev`:

```sh
cmake --build --preset dev --target feeders
cmake --build --preset dev --target providers
# Build both groups together:
cmake --build --preset dev --target feeders providers
```

These targets do not change which apps participate in a preset's default build.

The tables track the target exchange coverage. App links identify implemented examples and their
API references. All providers previously blocked by missing HTTPS support are implemented;
catalog scope is stated where it differs from the feeder. Unverified hosting cells remain explicit.
Existing market subdivisions remain separate.

Data-freshness monitoring belongs to each subscriber. Application heartbeats and acknowledgements
arrive as ordinary WebSocket messages, so only the subscriber can decide which messages refresh
its data deadline. The examples do not currently implement a subscriber freshness timer; use
[`nom::timer`](../docs/asio_comm/timer.md) for that policy. Control-Ping monitoring remains a
separate [WebSocket connection policy](../docs/asio_comm/web_socket_ping.md).

## Market-data feeders

These public WebSocket examples use Assembly/relay. JSON feeds log raw payloads; binary feeds log
only message type and size. Basic string checks identify API errors and heartbeats. They do not
construct a market-data model, reconstruct order books,
or automatically reconnect. App names link to their build instructions, configuration and workflows;
source comments explain subscription limits and API decisions.

Application heartbeat and subscription-pacing waits use [`nom::timer`](../docs/asio_comm/timer.md).
Cancellation suppresses pending callbacks, including expiries already queued for delivery.

Every feeder accepts `--exit-after-sec SECONDS`: omitted means unlimited, a positive value counts
from the completed WebSocket handshake (independent of received data), and `0` closes immediately
after connection before subscribing. Connection setup retains its separate WebSocket deadline.
Ctrl+C (`SIGINT`) and `SIGTERM` work during setup and streaming. Intentional stops exit **0**;
connection failures and unexpected disconnects exit **1**. Stops cancel owned timers and signal
waits, close the transport, drain callbacks and flush logs; they do not wait for a WebSocket closing
handshake. The `asio_comm_feeder_exit` integration suite checks this contract for all feeders using
local TLS peers.

Feeders use the `app_terminator` Assembly node from [common_app_nodes.h](common_app_nodes.h),
before every other stop handler. It owns signals, an optional `nom::timer` and the exit result; clients
relay connection/failure events and own their exchange-specific timers. Relay forwarding starts after
the originating node, so this ordering is required for the stop command to reach every interested node.

Frequencies below describe exchange publication intervals, not guaranteed delivery latency.
Event-driven streams can be silent when nothing changes. Locations concern serving infrastructure,
not company headquarters; see the dated [hosting evidence](#hosting-evidence) below.

| Exchange | Feeder app | Data, format and frequency | Possible hosting location |
| --- | --- | --- | --- |
| Binance Spot | [binance_spot_feeder](binance/binance_spot_feeder/README.md) | Best bid/ask (`bookTicker`) and individual trades (`trade`), event-driven; incremental depth at **100 ms** by default (`depth@100ms`), or **1000 ms** with `depth`. Raw JSON event objects; depth levels are `[price, quantity]` string pairs. [Stream specification](https://github.com/binance/binance-spot-api-docs/blob/master/web-socket-streams.md). | **AWS Tokyo, `ap-northeast-1`**: current `stream.binance.com` DNS addresses map to this region. Endpoint observation, not matching-engine proof. |
| Binance Spot (SBE) | [binance_spot_sbe_feeder](binance/binance_spot_sbe_feeder/README.md) | SBE trades and best bid/ask (event-driven), depth changes (20 ms). Logs only template name/ID and byte count; JSON control replies are identified as `JSON:control`. Requires `BINANCE_SBE_API_KEY` (Ed25519). See app README for official references and limits. | Not verified. |
| Binance USDⓈ-M perpetuals (default: USDT-margined BTCUSDT) | [binance_perp_feeder](binance/binance_perp_feeder/README.md) | Best bid/ask (`bookTicker`), individual trades (`trade`), and incremental depth at **100 ms** by default; plain `depth` uses the server default. Raw JSON event objects and `[price, quantity]` depth pairs. Book ticker is event-driven; raw trade timing has no published guarantee. [Public stream catalog](https://developers.binance.com/en/docs/catalog/core-trading-derivatives-trading-usd-s-m-futures/api/ws-streams/public). Raw `trade` is an observed, undocumented channel; see the app README's dated verification. | **AWS Tokyo, `ap-northeast-1`**: independently checked `fstream.binance.com` DNS addresses map to this region. Endpoint observation, not matching-engine proof. |
| Coinbase | [coinbase_spot_feeder](coinbase/coinbase_spot_feeder/README.md) | Spot ticker/BBO, individual matches and full-book snapshot then `level2_batch` changes (50 ms). JSON objects; heartbeat every second. See app README for official references and limits. | Not verified. |
| Upbit | [upbit_spot_feeder](upbit/upbit_spot_feeder/README.md) | Singapore Spot: ticker, trades and order-book snapshots in full-field JSON, initially and on updates. Uses `SGD-BTC` and the regional endpoint; publication follows channel events. See app README for official references and limits. | Not verified. |
| OKX Spot | [okex_spot_feeder](okx/okex_spot_feeder/README.md) | Ticker/BBO at most once per **100 ms** on changes; trades pushed on executions, potentially aggregating matches; **400-level** book snapshot then **100 ms** changes. JSON `arg`/`data` envelope, with `action` for books; levels contain price, size and auxiliary fields. [Tickers](https://www.okx.com/docs-v5/en/#order-book-trading-market-data-ws-tickers-channel), [trades](https://www.okx.com/docs-v5/en/#order-book-trading-market-data-ws-trades-channel), [books](https://www.okx.com/docs-v5/en/#order-book-trading-market-data-ws-order-book-channel). | **Hong Kong is a candidate**, supported by historical AWS/Alibaba Cloud infrastructure evidence. Current `ws.okx.com` resolves through Cloudflare; its origin cloud, region and facility are unconfirmed. See evidence below. |
| Bybit | [bybit_spot_feeder](bybit/bybit_spot_feeder/README.md) | V5 Spot ticker plus level-1 BBO snapshots, public trades and 50-level book snapshot/deltas (20 ms). JSON; separate application ping. See app README for official references and limits. | Not verified. |
| Bitget | [bitget_spot_feeder](bitget/bitget_spot_feeder/README.md) | V3 Spot ticker/BBO (200 ms), public trades (event-driven), full book snapshot then changes (50 ms). JSON `arg`/`data` envelopes. See app README for official references and limits. | Not verified. |
| Gate | [gate_spot_feeder](gate/gate_spot_feeder/README.md) | Spot book ticker (10 ms), trades (event-driven) and 20-level snapshots (100 ms). JSON channel/event/result envelopes. See app README for official references and limits. | Not verified. |
| KuCoin | [kucoin_spot_feeder](kucoin/kucoin_spot_feeder/README.md) | Public V2 Spot ticker, level-1 BBO and trades (event-driven), 50-level book snapshots (100 ms). JSON `T`/`d` envelopes; current public endpoint needs no Classic bullet token. See app README for official references and limits. | Not verified. |
| MEXC | [mexc_spot_feeder](mexc/mexc_spot_feeder/README.md) | Spot aggregated-delivery BBO, deals and depth channels (100 ms), encoded with Protobuf. Logs only wrapper channel/type and message byte count. See app README for official references and limits. | Not verified. |
| HTX | [htx_spot_feeder](htx/htx_spot_feeder/README.md) | Spot BBO/trades and ungrouped book snapshots, gzip-compressed JSON. Logs only channel/type and compressed byte count; inflates to handle heartbeat/error tokens. Requires zlib. See app README for official references and limits. | Not verified. |
| Crypto.com | [crypto_com_spot_feeder](crypto_com/crypto_com_spot_feeder/README.md) | Spot ticker/BBO, trades and ten-level books; JSON method/result/data envelopes. One batched subscription and server-initiated application heartbeats. See app README for official references and limits. | Not verified. |
| Kraken | [kraken_spot_feeder](kraken/kraken_spot_feeder/README.md) | V2 Spot BBO-triggered ticker, trades and 100-level book snapshot then updates; event-driven JSON channel/type/data envelopes. See app README for official references and limits. | Not verified. |
| LBank | [lbank_spot_feeder](lbank/lbank_spot_feeder/README.md) | Spot tick summary, trades and 100-level depth snapshots; JSON objects. Publication intervals are not specified by the linked reference. Echoes application heartbeat tokens. See app README for official references and limits. | Not verified. |
| XT.com | [xt_spot_feeder](xt/xt_spot_feeder/README.md) | Spot ticker (1 s), individual trades (event-driven), 20-level depth snapshots (1 s). JSON topic/event/data envelopes and text ping/pong. See app README for official references and limits. | Not verified. |
| bitFlyer Spot (Japan) | [bitflyer_spot_feeder](bitflyer/bitflyer_spot_feeder/README.md) | Ticker/BBO, executions, board snapshots and changes. JSON-RPC `channelMessage`, with an object or execution array in `params.message`. Executions and board changes are event-driven; ticker/snapshot publication is throttled without a documented fixed interval. [Ticker](https://bf-lightning-api.readme.io/docs/realtime-ticker), [executions](https://bf-lightning-api.readme.io/docs/realtime-executions), [board changes](https://bf-lightning-api.readme.io/docs/realtime-board), [snapshots](https://bf-lightning-api.readme.io/docs/realtime-board-snapshot). | **Azure Japan East (`japaneast`), Tokyo/Saitama**: observed WebSocket DNS chain names that region. Exact facility and matching-engine location are unverified. |
| Deribit perpetuals (default: inverse BTC-PERPETUAL) | [deribit_perp_feeder](deribit/deribit_perp_feeder/README.md) | Ticker/BBO and contract statistics, trade arrays, full-depth book snapshot then changes. JSON-RPC `subscription` notifications in `params.data`; book entries are `[action, price, amount]`. All selected channels use **100 ms** aggregation. [Ticker](https://docs.deribit.com/subscriptions/market-data/tickerinstrument_nameinterval), [trades](https://docs.deribit.com/subscriptions/trades/tradesinstrument_nameinterval), [book](https://docs.deribit.com/subscriptions/orderbook/bookinstrument_nameinterval). | **Equinix LD4, Slough, UK**: [exchange-confirmed infrastructure](https://support.deribit.com/hc/en-us/articles/25944617582877-Server-Infrastructure). The example uses the public, load-balanced `www.deribit.com` endpoint. |
| Hyperliquid Spot | [hyperliquid_spot_feeder](hyperliquid/hyperliquid_spot_feeder/README.md) | BBO and trades, plus L2 book snapshots at least 500 ms apart. JSON channel/data envelopes; spot pair indexes come from `spotMeta`. See app README for official references and limits. | Not verified. |
| Hyperliquid perpetuals | [hyperliquid_perp_feeder](hyperliquid/hyperliquid_perp_feeder/README.md) | Default perpetual DEX: BBO and trades, plus L2 book snapshots at least 500 ms apart. JSON channel/data envelopes; coin names come from `meta`. See app README for official references and limits. | Not verified. |

### Hosting evidence

Reviewed **2026-11-02**. DNS observations are from this development environment's resolver;
traffic steering, failover and endpoint overrides can change the result. They identify the public
serving endpoint, not necessarily the engine that produced the data. No comparative latency test
was performed; measure the actual subscriptions from candidate regions before choosing deployment.

- **Binance Spot and USDⓈ-M:** all eight IPv4 addresses returned for each hostname matched
  `AMAZON`/`EC2` prefixes in `ap-northeast-1` in the
  [AWS address-range file](https://ip-ranges.amazonaws.com/ip-ranges.json)
  (`createDate: 2026-11-02-07-27-05`). Representative addresses were `13.158.66.47` for Spot
  and `35.72.220.177` for USDⓈ-M. AWS identifies that region as
  [Asia Pacific (Tokyo)](https://docs.aws.amazon.com/global-infrastructure/latest/regions/aws-regions.html).
  These are observations, not addresses to hardcode.
- **bitFlyer:** observed chain:
  `ws.lightstream.bitflyer.com` → `lightstream-ws.trafficmanager.net` →
  `horizon-f5-lspejpv4.japaneast.cloudapp.azure.com` (`135.149.75.36`). This supports
  Azure Japan East for the current endpoint; Microsoft lists the region's physical location as
  [Tokyo/Saitama](https://learn.microsoft.com/en-us/azure/reliability/regions-list).
- **Deribit:** its [server infrastructure page](https://support.deribit.com/hc/en-us/articles/25944617582877-Server-Infrastructure)
  explicitly places infrastructure in LD4 and identifies `www.deribit.com` as the load-balanced
  REST/WebSocket entry point. This example does not use colocation or private connectivity.
- **OKX:** [Zenlayer's November 2023 case study](https://zenlayer.medium.com/case-study-okx-cuts-inter-cloud-latency-to-2-ms-with-zenlayer-cloud-networking-3403ad7b5141)
  describes OKX workloads across AWS/Alibaba Cloud and interconnections in Hong Kong and the eastern
  US, with plans spanning Hong Kong, Singapore and Tokyo. It does not locate this specific Spot feed.
  Current DNS returned `ws.okx.com.cdn.cloudflare.net` (`104.18.43.174`, `172.64.144.82`), so it
  does not establish the origin location. Hong Kong remains a deployment candidate, not a confirmed
  current WebSocket origin.

## Symbol providers

The providers fetch an initial catalog over WebSocket or HTTPS and preserve its JSON response
envelope on stdout. WebSocket acknowledgements/status messages do not complete a catalog request;
HTTPS providers require HTTP 200 and the expected success envelope. Basic string checks identify
expected fields and API errors; there is no JSON parser, normalization or instrument filtering.
Catalogs may include inactive instruments or other product types; consult each row/app for scope.
The examples below are abbreviated snapshots, not current trading rules. Binance SBE feeds can
use the existing Binance Spot JSON symbol catalog.

| Exchange | App | Example symbols | Symbol information example / catalog scope |
| --- | --- | --- | --- |
| Binance Spot | [binance_spot_symbol_provider](binance/binance_spot_symbol_provider/README.md) | `BTCUSDT`, `ETHUSDT`, `SOLUSDT` | `{"symbol":"BTCUSDT","status":"TRADING","baseAsset":"BTC","quoteAsset":"USDT","baseAssetPrecision":8,"quoteAssetPrecision":8,"filters":[{"filterType":"PRICE_FILTER","minPrice":"0.01","maxPrice":"1000000.00","tickSize":"0.01"},{"filterType":"LOT_SIZE","minQty":"0.00001","maxQty":"9000.00000","stepSize":"0.00001"}]}` |
| Binance USDⓈ-M perpetuals (default: USDT-margined BTCUSDT) | [binance_perp_symbol_provider](binance/binance_perp_symbol_provider/README.md) | `BTCUSDT`, `ETHUSDT`, `SOLUSDT` | HTTPS `GET /fapi/v1/exchangeInfo`. Full USDⓈ-M catalog, including dated/inactive contracts; select `contractType=PERPETUAL` downstream. Example: `{"symbol":"BTCUSDT","contractType":"PERPETUAL","baseAsset":"BTC","quoteAsset":"USDT"}` |
| Coinbase | [coinbase_spot_symbol_provider](coinbase/coinbase_spot_symbol_provider/README.md) | `BTC-USD`, `ETH-USD`, `SOL-USD` | `{"id":"BTC-USD","base_currency":"BTC","quote_currency":"USD","quote_increment":"0.01"}` |
| Upbit | [upbit_spot_symbol_provider](upbit/upbit_spot_symbol_provider/README.md) | `SGD-BTC`, `SGD-ETH`, `SGD-SOL` | HTTPS `GET /v1/market/all?is_details=true`. Singapore market codes/names/warnings; not full tick/lot rules. Example: `{"market":"SGD-BTC","english_name":"Bitcoin","market_warning":"NONE"}` |
| OKX Spot | [okx_spot_symbol_provider](okx/okx_spot_symbol_provider/README.md) | `BTC-USDT`, `ETH-USDT`, `SOL-USDT` | HTTPS `GET /api/v5/public/instruments?instType=SPOT`. Spot instrument specifications and state. Example: `{"instId":"BTC-USDT","instType":"SPOT","baseCcy":"BTC","quoteCcy":"USDT"}` |
| Bybit | [bybit_spot_symbol_provider](bybit/bybit_spot_symbol_provider/README.md) | `BTCUSDT`, `ETHUSDT`, `SOLUSDT` | HTTPS `GET /v5/market/instruments-info?category=spot`. Spot specifications; no Spot pagination. Example: `{"symbol":"BTCUSDT","baseCoin":"BTC","quoteCoin":"USDT","status":"Trading"}` |
| Bitget | [bitget_spot_symbol_provider](bitget/bitget_spot_symbol_provider/README.md) | `BTCUSDT`, `ETHUSDT`, `SOLUSDT` | HTTPS `GET /api/v3/market/instruments?category=SPOT`. V3 Spot catalog, including tokenized assets. Example: `{"symbol":"BTCUSDT","category":"SPOT","baseCoin":"BTC","quoteCoin":"USDT"}` |
| Gate | [gate_spot_symbol_provider](gate/gate_spot_symbol_provider/README.md) | `BTC_USDT`, `ETH_USDT`, `SOL_USDT` | HTTPS `GET /api/v4/spot/currency_pairs`. Pair specifications, precision and trading status. Example: `{"id":"BTC_USDT","base":"BTC","quote":"USDT","trade_status":"tradable"}` |
| KuCoin | [kucoin_spot_symbol_provider](kucoin/kucoin_spot_symbol_provider/README.md) | `BTC-USDT`, `ETH-USDT`, `SOL-USDT` | HTTPS `GET /api/v2/symbols`. Pair specifications, increments and minimum funds. Example: `{"symbol":"BTC-USDT","baseCurrency":"BTC","quoteCurrency":"USDT","enableTrading":true}` |
| MEXC | [mexc_spot_symbol_provider](mexc/mexc_spot_symbol_provider/README.md) | `BTCUSDT`, `ETHUSDT`, `SOLUSDT` | HTTPS `GET /api/v3/exchangeInfo`. JSON API catalog for the Protobuf market-data feeder. Example: `{"symbol":"BTCUSDT","baseAsset":"BTC","quoteAsset":"USDT","status":"1"}` |
| HTX | [htx_spot_symbol_provider](htx/htx_spot_symbol_provider/README.md) | `btcusdt`, `ethusdt`, `solusdt` | HTTPS `GET /v2/settings/common/symbols`. Full snapshot (`full=1`); includes offline entries. Example: `{"sc":"btcusdt","bc":"btc","qc":"usdt","state":"online"}` |
| Crypto.com | [crypto_com_spot_symbol_provider](crypto_com/crypto_com_spot_symbol_provider/README.md) | `BTC_USDT`, `ETH_USDT`, `SOL_USDT` | HTTPS `GET /exchange/v1/public/get-instruments`. Mixed product catalog; select `inst_type=CCY_PAIR` downstream. Example: `{"symbol":"BTC_USDT","inst_type":"CCY_PAIR","base_ccy":"BTC","quote_ccy":"USDT"}` |
| Kraken | [kraken_spot_symbol_provider](kraken/kraken_spot_symbol_provider/README.md) | `BTC/USD`, `ETH/USD`, `SOL/USD` | `{"symbol":"BTC/USD","base":"BTC","quote":"USD","price_increment":0.1,"qty_increment":1e-08}` |
| LBank | [lbank_spot_symbol_provider](lbank/lbank_spot_symbol_provider/README.md) | `btc_usdt`, `eth_usdt`, `sol_usdt` | HTTPS `GET /v2/accuracy.do`. Pair specifications and precision, not just names. Example: `{"symbol":"btc_usdt","priceAccuracy":"2","quantityAccuracy":"5"}` |
| XT.com | [xt_spot_symbol_provider](xt/xt_spot_symbol_provider/README.md) | `btc_usdt`, `eth_usdt`, `sol_usdt` | HTTPS `GET /v4/public/symbol`. Full symbol list with filters; no cached-version suppression. Example: `{"symbol":"btc_usdt","state":"ONLINE","baseCurrency":"btc","quoteCurrency":"usdt"}` |
| bitFlyer Spot (Japan) | [bitflyer_spot_symbol_provider](bitflyer/bitflyer_spot_symbol_provider/README.md) | `BTC_JPY`, `ETH_JPY`, `XRP_JPY` | HTTPS `GET /v1/markets`. Japan market codes/types; includes FX/CFD. Select `market_type=Spot`; tick/lot rules are not supplied. Example: `{"product_code":"BTC_JPY","market_type":"Spot"}` |
| Deribit perpetuals (default: inverse BTC-PERPETUAL) | [deribit_perp_symbol_provider](deribit/deribit_perp_symbol_provider/README.md) | `BTC-PERPETUAL`, `ETH-PERPETUAL`, `BTC_USDC-PERPETUAL` | `{"instrument_name":"BTC-PERPETUAL","base_currency":"BTC","quote_currency":"USD","settlement_period":"perpetual"}` |
| Hyperliquid Spot | [hyperliquid_spot_symbol_provider](hyperliquid/hyperliquid_spot_symbol_provider/README.md) | `PURR/USDC`, `@1`, `@2` | `{"name":"PURR/USDC","tokens":[1,0],"index":0}` |
| Hyperliquid perpetuals | [hyperliquid_perp_symbol_provider](hyperliquid/hyperliquid_perp_symbol_provider/README.md) | `BTC`, `ETH`, `SOL` | `{"name":"BTC","szDecimals":5,"maxLeverage":40}` |

### Implementation and validation

On **2026-11-05**, the 13 HTTPS-backed symbol providers were implemented with `nom::https`,
then each fetched its production catalog successfully over **HTTP/1.1**. None remains skipped
for lack of HTTPS or a requirement for HTTP/2. These were single public requests without credentials;
regional access rules and future endpoint changes can still affect deployment.

Each HTTPS provider sends one GET, requests uncompressed JSON, and closes after relaying the
catalog. The default **16 MiB** body limit is explicit to these snapshot apps and configurable with
`--max-response-size`; shared HTTPS defaults are unchanged. There is no polling, retry, redirect
following or shared rate limiter. Per-app notes explain request options, quotas and catalog scope.
The `asio_comm_https_symbol_providers` local TLS suite covers all 13 apps: exact requests,
chunked/large responses, unchanged stdout JSON, API/HTTP errors, unexpected/compressed replies,
body limits, disconnects and signal cancellation; HTX incremental responses are rejected.
The suite passed in Debug and ASan/UBSan builds on the same date.

[common_app_nodes.h](common_app_nodes.h) defines `asio_service`, `app_terminator`,
`service_command` and `feed_event`. The shared terminator accepts each feeder's config through its
`exit_after_sec` field. One-shot providers retain their own completion and signal policy; their local
`provider_state` extends the shared I/O context where necessary. Exchange clients, output nodes and
assembly composition remain explicit in each `main.cpp`.

All listed feeders and symbol providers have build targets and individual READMEs. Each app keeps
defaults, argument parsing and `app_config::validate()` in its own `config.h`.
Validation returns `std::expected<void, std::string>` without modifying the configuration. The exchange
client, output node, and assembly/relay wiring remain in `main.cpp`. Connection callbacks,
subscriptions and any heartbeat timers are directly visible in that client node. The HTX feeder
additionally links zlib. These examples do not execute orders.

On **2026-11-02**, the five new symbol providers fetched production catalogs successfully. All 14
new feeders that require no credentials received production data during short subscriptions.
Binance SBE production access remains unverified without an Ed25519 API key; local fixtures cover
its authentication header, template identification and suppression of market payloads.

The `asio_comm_exchange_apps` integration suite checks the new apps with local TLS peers, including
two-symbol subscriptions, acknowledgement exclusion, fragmented messages, application/control
heartbeats, binary log suppression and shutdown. Live checks establish protocol compatibility at
one point in time; they do not establish lossless capture or sequence reconciliation.

## Diagnostics and general examples

- [binance_spot_resubscribe_test](binance/binance_spot_resubscribe_test/README.md): start a Spot subscription,
  then add symbols directly from the Nth market-data callback on the same connection.
- [web_socket_example](web_socket_example/README.md): connect, optionally send a message, print the
  first reply and close; includes client parameter examples.
- [https_example](https_example/README.md): verified TLS, GET responses and request submission from response callbacks.
- [timer_example](timer_example/README.md): one-shot timer rearming and expiry delivery through Assembly/relay.
- [asio_comm_example](asio_comm_example/README.md): configure logging and run a local Assembly/relay workflow.
