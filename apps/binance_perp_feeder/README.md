# Binance perpetual futures feeder

`binance_perp_feeder` logs public Binance USDⓈ-M perpetual futures market data using
`nom::web_socket` and `spd_logger`. Defaults are `BTCUSDT` with `ticker`, `trade`, and `depth`.

## Build and run

Follow the shared [build prerequisites](../../docs/kit/build.md#toolchain). From the repository root:

```bash
cmake --preset dev
cmake --build --preset dev --target binance_perp_feeder
./build/dev/bin/binance_perp_feeder
./build/dev/bin/binance_perp_feeder --symbols BTCUSDT,ETHUSDT --channels trade,depth
./build/dev/bin/binance_perp_feeder --channels ticker
./build/dev/bin/binance_perp_feeder --help
```

The app runs until Ctrl+C or a connection ends. A connection error or disconnection closes
all active feeds and exits with status 1. There is no automatic reconnect. A 30-second deadline
covers setup of all selected connections.

## Configuration and routing

Settings live in `app_config` in [main.cpp](main.cpp), including `nom::log_config`.
`--symbols` and `--channels` accept comma-separated lists. Symbols accept ASCII letters and
digits and are normalized to lowercase; channel names are case-sensitive. Use perpetual
contract symbols such as `BTCUSDT` or `ETHUSDT`. No configuration file or JSON configuration
library is needed.

Binance routes these streams to two endpoints. The app opens only the routes needed by the
selected channels, sends one `SUBSCRIBE` request per connection, and runs both on one
`io_context` thread.

| Channel | Subscription stream | Route | Data |
| --- | --- | --- | --- |
| `ticker` | `<symbol>@ticker` | `/market/ws` | Rolling 24-hour statistics, every two seconds |
| `trade` | `<symbol>@aggTrade` | `/market/ws` | Aggregate trade updates, every 100 ms |
| `depth` | `<symbol>@depth` | `/public/ws` | Incremental depth updates, default 250 ms |

The futures `trade` option selects **aggregate trades**, unlike the Spot example's individual trades.
See Binance's [market streams](https://developers.binance.com/en/docs/catalog/core-trading-derivatives-trading-usd-s-m-futures/api/ws-streams/market)
and [public streams](https://developers.binance.com/en/docs/catalog/core-trading-derivatives-trading-usd-s-m-futures/api/ws-streams/public)
for payloads and update intervals.

Defaults are `wss://fstream.binance.com/market/ws` and `wss://fstream.binance.com/public/ws`.
`--market-endpoint` and `--public-endpoint` override them, for example when testing with a local TLS peer.
An unrouted `/ws` connection does not deliver market-route streams under Binance's current
[connection rules](https://developers.binance.com/en/docs/products/derivatives-trading-usds-futures/websocket-market-streams/Connect).
Each connection supports up to 1024 streams and expires after 24 hours.

The WebSocket client answers server ping frames while reading. This app also enables outbound
pings so incoming pongs keep its idle timer active on quiet subscriptions; Binance's server pings
alone are three minutes apart.

Every received payload is logged unchanged, with a session identifier identifying its route.
Subscription acknowledgements and API errors are logged too. The app does not parse them,
reconstruct an order book, or store data separately. The [WebSocket constraints](../../docs/web_socket.md#usage-constraints-and-deferred-work)
describe the client's execution and lifetime contract.

## Validation

The local TLS integration checks verify routed subscriptions, aggregate trade names, multiple
symbols, selection of a single route, logging, ping/pong handling, and closing both connections
when one disconnects. They need no Binance access.

See the [Spot feeder](../binance_spot_feeder/README.md) and [project overview](../../README.md).
