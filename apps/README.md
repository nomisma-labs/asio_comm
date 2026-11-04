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

The tables track the target exchange coverage. App links identify implemented examples; skipped symbol
providers include the reason and API reference. Unverified hosting cells remain explicit. Existing market subdivisions remain separate.

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

The implemented providers fetch a complete initial catalog over WebSocket and preserve its JSON
response envelope on stdout. Acknowledgements and unrelated status messages do not complete the
request. Basic string checks identify expected catalog fields and API errors; there is no JSON
parser or normalized symbol model. The examples below are abbreviated snapshots, not current
trading rules. Binance SBE feeds can use the existing Binance Spot JSON symbol catalog.

| Exchange | App | Example symbols | Symbol information example or skip reason |
| --- | --- | --- | --- |
| Binance Spot | [binance_spot_symbol_provider](binance/binance_spot_symbol_provider/README.md) | `BTCUSDT`, `ETHUSDT`, `SOLUSDT` | `{"symbol":"BTCUSDT","status":"TRADING","baseAsset":"BTC","quoteAsset":"USDT","baseAssetPrecision":8,"quoteAssetPrecision":8,"filters":[{"filterType":"PRICE_FILTER","minPrice":"0.01","maxPrice":"1000000.00","tickSize":"0.01"},{"filterType":"LOT_SIZE","minQty":"0.00001","maxQty":"9000.00000","stepSize":"0.00001"}]}` |
| Binance USDⓈ-M perpetuals (default: USDT-margined BTCUSDT) | Skipped | | Catalog uses REST `GET /fapi/v1/exchangeInfo`; the WS request API does not expose exchangeInfo. The contract-info stream supplies changes, not a full initial catalog. [REST reference](https://developers.binance.com/en/docs/catalog/core-trading-derivatives-trading-usd-s-m-futures/api/rest-api/market-data). |
| Coinbase | [coinbase_spot_symbol_provider](coinbase/coinbase_spot_symbol_provider/README.md) | `BTC-USD`, `ETH-USD`, `SOL-USD` | `{"id":"BTC-USD","base_currency":"BTC","quote_currency":"USD","quote_increment":"0.01"}` |
| Upbit | Skipped | | The documented catalog is HTTP `GET /v1/market/all`; WebSocket requires known codes. [Catalog reference](https://global-docs.upbit.com/reference/list-trading-pairs). |
| OKX Spot | Skipped | | WebSocket instruments publishes parameter/listing changes; a full initial catalog requires REST `GET /api/v5/public/instruments`. [REST and instrument stream](https://www.okx.com/docs-v5/en/#public-data-rest-api-get-instruments). |
| Bybit | Skipped | | Instrument discovery uses HTTP `GET /v5/market/instruments-info`; public WS topics require known symbols. [Reference](https://bybit-exchange.github.io/docs/v5/market/instrument). |
| Bitget | Skipped | | Instrument discovery is documented as REST `GET /api/v3/market/instruments`; the public WS catalog has no initial instrument-list request. [Market API](https://www.bitget.com/docs/catalog/market-market-data/market-instruments). |
| Gate | Skipped | | Full pair specifications use HTTP `GET /spot/currency_pairs`; WS market subscriptions require known pair names. [REST API](https://www.gate.com/docs/developers/apiv4/en/). |
| KuCoin | Skipped | | Full symbol specifications use the REST symbol endpoints; the public V2 WS channels deliver market data for supplied symbols. [Get symbol](https://www.kucoin.com/docs-new/rest/spot-trading/market-data/get-symbol). |
| MEXC | Skipped | | Instrument discovery uses HTTP `GET /api/v3/exchangeInfo`; Protobuf WS channels contain market updates, not the catalog. [Reference](https://mexcdevelop.github.io/apidocs/spot_v3_en/#exchange-information). |
| HTX | Skipped | | The supported-symbol catalog uses HTTP `GET /v2/settings/common/symbols`; market WS channels require a symbol. [Reference data](https://huobiapi.github.io/docs/spot/v1/en/#get-all-supported-trading-symbol-v2). |
| Crypto.com | Skipped | | The current API documents `public/get-instruments` under REST; the public market WS methods provide subscriptions and heartbeats. [Catalog](https://exchange-developer.crypto.com/exchange/v1/docs/api/rest-single-page/), [WS methods](https://exchange-developer.crypto.com/exchange/v1/docs/api/websocket-single-page/). |
| Kraken | [kraken_spot_symbol_provider](kraken/kraken_spot_symbol_provider/README.md) | `BTC/USD`, `ETH/USD`, `SOL/USD` | `{"symbol":"BTC/USD","base":"BTC","quote":"USD","price_increment":0.1,"qty_increment":1e-08}` |
| LBank | Skipped | | Pair specifications use HTTP `GET /v2/accuracy.do` (names: `/v2/currencyPairs.do`); no WS catalog request is documented. [Reference](https://www.lbank.com/docs/index.html#trading-pairs). |
| XT.com | Skipped | | Pair metadata uses HTTP `/v4/public/symbol`; public WS topics require known symbols. [Market API](https://doc.xt.com/docs/spot/Market/GetSymbolInformation). |
| bitFlyer Spot (Japan) | Skipped | | Discovery uses HTTP `GET /v1/markets`; the Realtime JSON-RPC API subscribes to known product codes. No complete instrument catalog over WS is documented. [API reference](https://lightning.bitflyer.com/docs). |
| Deribit perpetuals (default: inverse BTC-PERPETUAL) | [deribit_perp_symbol_provider](deribit/deribit_perp_symbol_provider/README.md) | `BTC-PERPETUAL`, `ETH-PERPETUAL`, `BTC_USDC-PERPETUAL` | `{"instrument_name":"BTC-PERPETUAL","base_currency":"BTC","quote_currency":"USD","settlement_period":"perpetual"}` |
| Hyperliquid Spot | [hyperliquid_spot_symbol_provider](hyperliquid/hyperliquid_spot_symbol_provider/README.md) | `PURR/USDC`, `@1`, `@2` | `{"name":"PURR/USDC","tokens":[1,0],"index":0}` |
| Hyperliquid perpetuals | [hyperliquid_perp_symbol_provider](hyperliquid/hyperliquid_perp_symbol_provider/README.md) | `BTC`, `ETH`, `SOL` | `{"name":"BTC","szDecimals":5,"maxLeverage":40}` |

### Implementation and validation

All 15 previously missing feeders and five additional symbol providers now have build targets and
individual READMEs. Each app keeps defaults, argument parsing and `app_config::validate()` in its own `config.h`.
Validation returns `std::expected<void, std::string>` without modifying the configuration. The exchange
client, output node, and assembly/relay wiring remain in `main.cpp`. Connection callbacks,
subscriptions and any heartbeat timers are directly visible in that client node. HTX additionally
links zlib. These examples do not execute orders.

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
