# Example applications

[Project overview and shared build instructions](../README.md)

The tables track the target exchange coverage. Empty cells mark missing implementations for future
work; populated rows describe the apps available today. Existing market subdivisions remain separate.

## Market-data feeders

These public WebSocket examples use Assembly/relay and log incoming payloads unchanged,
including acknowledgements and errors. They do not parse market data, reconstruct order books,
or automatically reconnect. App names link to their build instructions, configuration and workflows;
source comments explain subscription limits and API decisions.

Frequencies below describe exchange publication intervals, not guaranteed delivery latency.
Event-driven streams can be silent when nothing changes. Locations concern serving infrastructure,
not company headquarters; see the dated [hosting evidence](#hosting-evidence) below.

| Exchange | Feeder app | Data, format and frequency | Possible hosting location |
| --- | --- | --- | --- |
| Binance Spot | [binance_spot_feeder](binance_spot_feeder/README.md) | Best bid/ask (`bookTicker`) and individual trades (`trade`), event-driven; incremental depth at **100 ms** by default (`depth@100ms`), or **1000 ms** with `depth`. Raw JSON event objects; depth levels are `[price, quantity]` string pairs. [Stream specification](https://github.com/binance/binance-spot-api-docs/blob/master/web-socket-streams.md). | **AWS Tokyo, `ap-northeast-1`**: current `stream.binance.com` DNS addresses map to this region. Endpoint observation, not matching-engine proof. |
| Binance Spot (SBE) | | | |
| Binance USDⓈ-M perpetuals (default: USDT-margined BTCUSDT) | [binance_perp_feeder](binance_perp_feeder/README.md) | Best bid/ask (`bookTicker`), individual trades (`trade`), and incremental depth at **100 ms** by default; plain `depth` uses the server default. Raw JSON event objects and `[price, quantity]` depth pairs. Book ticker is event-driven; raw trade timing has no published guarantee. [Public stream catalog](https://developers.binance.com/en/docs/catalog/core-trading-derivatives-trading-usd-s-m-futures/api/ws-streams/public). Raw `trade` is an observed, undocumented channel; see the app README's dated verification. | **AWS Tokyo, `ap-northeast-1`**: independently checked `fstream.binance.com` DNS addresses map to this region. Endpoint observation, not matching-engine proof. |
| Coinbase | | | |
| Upbit | | | |
| OKX Spot | [okex_spot_feeder](okex_spot_feeder/README.md) | Ticker/BBO at most once per **100 ms** on changes; trades pushed on executions, potentially aggregating matches; **400-level** book snapshot then **100 ms** changes. JSON `arg`/`data` envelope, with `action` for books; levels contain price, size and auxiliary fields. [Tickers](https://www.okx.com/docs-v5/en/#order-book-trading-market-data-ws-tickers-channel), [trades](https://www.okx.com/docs-v5/en/#order-book-trading-market-data-ws-trades-channel), [books](https://www.okx.com/docs-v5/en/#order-book-trading-market-data-ws-order-book-channel). | **Hong Kong is a candidate**, supported by historical AWS/Alibaba Cloud infrastructure evidence. Current `ws.okx.com` resolves through Cloudflare; its origin cloud, region and facility are unconfirmed. See evidence below. |
| Bybit | | | |
| Bitget | | | |
| Gate | | | |
| KuCoin | | | |
| MEXC | | | |
| HTX | | | |
| Crypto.com | | | |
| Kraken | | | |
| LBank | | | |
| XT.com | | | |
| bitFlyer Spot (Japan) | [bitflyer_spot_feeder](bitflyer_spot_feeder/README.md) | Ticker/BBO, executions, board snapshots and changes. JSON-RPC `channelMessage`, with an object or execution array in `params.message`. Executions and board changes are event-driven; ticker/snapshot publication is throttled without a documented fixed interval. [Ticker](https://bf-lightning-api.readme.io/docs/realtime-ticker), [executions](https://bf-lightning-api.readme.io/docs/realtime-executions), [board changes](https://bf-lightning-api.readme.io/docs/realtime-board), [snapshots](https://bf-lightning-api.readme.io/docs/realtime-board-snapshot). | **Azure Japan East (`japaneast`), Tokyo/Saitama**: observed WebSocket DNS chain names that region. Exact facility and matching-engine location are unverified. |
| Deribit perpetuals (default: inverse BTC-PERPETUAL) | [deribit_perp_feeder](deribit_perp_feeder/README.md) | Ticker/BBO and contract statistics, trade arrays, full-depth book snapshot then changes. JSON-RPC `subscription` notifications in `params.data`; book entries are `[action, price, amount]`. All selected channels use **100 ms** aggregation. [Ticker](https://docs.deribit.com/subscriptions/market-data/tickerinstrument_nameinterval), [trades](https://docs.deribit.com/subscriptions/trades/tradesinstrument_nameinterval), [book](https://docs.deribit.com/subscriptions/orderbook/bookinstrument_nameinterval). | **Equinix LD4, Slough, UK**: [exchange-confirmed infrastructure](https://support.deribit.com/hc/en-us/articles/25944617582877-Server-Infrastructure). The example uses the public, load-balanced `www.deribit.com` endpoint. |
| Hyperliquid Spot | | | |
| Hyperliquid perpetuals | | | |

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

Only the Binance Spot provider is implemented. It requests `exchangeInfo` once over WebSocket and writes
the complete JSON response to stdout, including the `result.symbols` array. The table shows selected
fields of one illustrative symbol, not a current trading-rule snapshot; the app does not extract
or normalize individual symbol records. See the
[exchangeInfo schema](https://github.com/binance/binance-spot-api-docs/blob/master/web-socket-api.md#exchange-information).

| Exchange | App | Example symbols | Example of one symbol's information (abbreviated) |
| --- | --- | --- | --- |
| Binance Spot | [binance_spot_symbol_provider](binance_spot_symbol_provider/README.md) | `BTCUSDT`, `ETHUSDT`, `SOLUSDT` | `{"symbol":"BTCUSDT","status":"TRADING","baseAsset":"BTC","quoteAsset":"USDT","baseAssetPrecision":8,"quoteAssetPrecision":8,"filters":[{"filterType":"PRICE_FILTER","minPrice":"0.01","maxPrice":"1000000.00","tickSize":"0.01"},{"filterType":"LOT_SIZE","minQty":"0.00001","maxQty":"9000.00000","stepSize":"0.00001"}]}` |
| Binance USDⓈ-M perpetuals (default: USDT-margined BTCUSDT) | | | |
| Coinbase | | | |
| Upbit | | | |
| OKX Spot | | | |
| Bybit | | | |
| Bitget | | | |
| Gate | | | |
| KuCoin | | | |
| MEXC | | | |
| HTX | | | |
| Crypto.com | | | |
| Kraken | | | |
| LBank | | | |
| XT.com | | | |
| bitFlyer Spot (Japan) | | | |
| Deribit perpetuals (default: inverse BTC-PERPETUAL) | | | |
| Hyperliquid Spot | | | |
| Hyperliquid perpetuals | | | |

Hyperliquid symbol providers can use WebSocket request/reply: wrap an info request in
`method: "post"`, using `type: "meta"` for perpetual metadata or `type: "spotMeta"` for spot
pairs and token metadata. These are catalog requests, not continuous listing subscriptions;
both implementations above remain pending. See [WebSocket post requests](https://hyperliquid.gitbook.io/hyperliquid-docs/for-developers/api/websocket/post-requests),
[perpetual metadata](https://hyperliquid.gitbook.io/hyperliquid-docs/for-developers/api/info-endpoint/perpetuals)
and [spot metadata](https://hyperliquid.gitbook.io/hyperliquid-docs/for-developers/api/info-endpoint/spot).

## Diagnostics and general examples

- [binance_spot_resubscribe_test](binance_spot_resubscribe_test/README.md): start a Spot subscription,
  then add symbols directly from the Nth market-data callback on the same connection.
- [web_socket_example](web_socket_example/README.md): connect, optionally send a message, print the
  first reply and close; includes client parameter examples.
- [asio_comm_example](asio_comm_example/README.md): configure logging and run a local Assembly/relay workflow.
