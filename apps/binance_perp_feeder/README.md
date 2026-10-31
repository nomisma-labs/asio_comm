# Binance perpetual futures feeder

`binance_perp_feeder` logs streaming Binance USDⓈ-M perpetual futures market data using
one `nom::web_socket` connection. Defaults are `BTCUSDT` with `bookTicker`, `trade`, and
`depth@100ms`. No API key is required.

## Build and run

Follow the shared [build prerequisites](../../docs/kit/build.md#toolchain). From the repository root:

```bash
cmake --preset dev
cmake --build --preset dev --target binance_perp_feeder
./build/dev/bin/binance_perp_feeder
./build/dev/bin/binance_perp_feeder --symbols BTCUSDT,ETHUSDT --channels trade,depth@100ms
./build/dev/bin/binance_perp_feeder --channels bookTicker
./build/dev/bin/binance_perp_feeder --help
```

The app runs until Ctrl+C or disconnection, then exits with status 1. There is no automatic
reconnect or application connection deadline. Client deadline handling remains WIP.

## Configuration and streams

Settings live in `app_config` in [main.cpp](main.cpp), including `nom::log_config`.
`--symbols`, `--channels`, and `--endpoint` override the C++ settings. Symbols accept ASCII
letters and digits and are normalized to lowercase. Channel names are case-sensitive and
are sent unchanged; supported values are `bookTicker`, `trade`, `depth`, and `depth@100ms`.
No configuration file or JSON configuration library is needed.

The default endpoint is `wss://fstream.binance.com/public/ws`. On connection, the app sends
one `SUBSCRIBE` request containing every selected symbol/channel combination:

```json
{"method":"SUBSCRIBE","params":["btcusdt@bookTicker","btcusdt@trade","btcusdt@depth@100ms"],"id":1}
```

| Channel | Received event | Data |
| --- | --- | --- |
| `bookTicker` | `bookTicker` | Best bid/ask price and quantity updates |
| `trade` | `trade` | Individual trades with a trade ID (`t`), price (`p`), quantity (`q`), and trade time (`T`) |
| `depth@100ms` | `depthUpdate` | Incremental order-book changes at the requested 100 ms interval |
| `depth` | `depthUpdate` | Incremental changes at the server's default interval |

Book ticker and depth are documented in Binance's
[public stream catalog](https://developers.binance.com/en/docs/catalog/core-trading-derivatives-trading-usd-s-m-futures/api/ws-streams/public).
Raw `trade` delivery was verified directly on the public endpoint on 2026-10-31: one connection
received `trade`, `bookTicker`, and `depthUpdate` events together. The current published
[market stream catalog](https://developers.binance.com/en/docs/catalog/core-trading-derivatives-trading-usd-s-m-futures/api/ws-streams/market)
lists aggregate trades but does not list this raw trade stream. Live behavior and published
API coverage are distinct evidence; this example subscribes to `trade` without substituting
`aggTrade`.

The WebSocket client answers server pings while reading. Outbound control pings are enabled
so received pongs keep the client's idle timer active on quiet subscriptions.

Every payload is logged unchanged, including subscription acknowledgements and API errors.
The app does not parse messages, reconcile snapshots, reconstruct an order book, or subscribe
to private data. See the [WebSocket constraints](../../docs/web_socket.md#usage-constraints-and-deferred-work).

## Assembly structure

`binance_perp_assembly` composes three nodes:

- `asio_service` owns the shared `boost::asio::io_context` in its context mixin.
- `binance_perp_client` owns one WebSocket and one subscription request for all selected streams.
- `market_data_logger` logs the raw payloads relayed by the client.

`app` starts the client through the relay and runs the context on one thread. Stop closes
that connection. Relay delivery is synchronous; payload/source views are borrowed only during
that call. The context outlives the nodes, and the app remains alive while the context runs.

## Validation

The local TLS integration checks verify one connection with multiple symbols and channels,
exact `trade` subscriptions, individual-trade payload logging, channel selection, ping/pong,
and disconnection cleanup. They use a local peer and do not establish exchange availability.

See the [Spot feeder](../binance_spot_feeder/README.md) and [project overview](../../README.md).
