# Binance perpetual futures feeder

`binance_perp_feeder` logs streaming Binance USDⓈ-M perpetual futures market data using
one `nom::web_socket` connection. Defaults are `BTCUSDT` with `bookTicker`, `trade`, and
`depth@100ms`. No API key is required.

## Build and run

Follow the shared [build prerequisites](../../../docs/kit/build.md#toolchain). From the repository root:

```bash
cmake --preset dev
cmake --build --preset dev --target binance_perp_feeder
./build/dev/bin/binance_perp_feeder
./build/dev/bin/binance_perp_feeder --symbols BTCUSDT,ETHUSDT --channels trade,depth@100ms
./build/dev/bin/binance_perp_feeder --channels bookTicker
./build/dev/bin/binance_perp_feeder --exit-after-sec 10
./build/dev/bin/binance_perp_feeder --exit-after-sec 0
./build/dev/bin/binance_perp_feeder --help
```

Without `--exit-after-sec`, the app has no runtime limit. A positive value counts seconds from
the completed WebSocket handshake, independently of received data. Zero closes immediately
after connection, before sending a subscription. Connection setup has the WebSocket client's
own deadline; the runtime limit does not include DNS/TCP/TLS/upgrade time.

Ctrl+C (`SIGINT`) and `SIGTERM` are handled on the I/O thread, including during connection setup.
Timeout and signal stops exit **0**; unexpected disconnection, connection failure and invalid
arguments exit **1**. Stop cancels the timer and signal wait, calls `web_socket::close()`, drains
pending callbacks and flushes the logger. This uses the client's immediate transport-close
contract; it does not wait for a WebSocket closing handshake. There is no automatic reconnect.

## Configuration and streams

Settings live in `app_config` in [config.h](config.h), including `nom::log_config`.
`--symbols`, `--channels`, `--endpoint`, and `--exit-after-sec` override the C++ settings.
The optional runtime limit accepts unsigned whole seconds, including zero; omission disables it.
Symbols accept ASCII letters and digits and are normalized to lowercase. Channel names are case-sensitive and
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

The WebSocket client answers server pings while reading. Outbound control pings check
connection availability independently of market-data activity. Data freshness belongs to the subscriber.

Every payload is logged unchanged, including subscription acknowledgements and API errors.
The app does not parse messages, reconcile snapshots, reconstruct an order book, or subscribe
to private data. See the [WebSocket constraints](../../../docs/asio_comm/web_socket.md#usage-constraints-and-deferred-work).

## Assembly structure

The I/O node, terminator and lifecycle events are shared in [common_app_nodes.h](../../common_app_nodes.h).
Exchange nodes and assembly composition remain in [main.cpp](main.cpp).

`binance_perp_assembly` composes four nodes:

- `asio_service` owns the shared I/O context in its context mixin.
- `app_terminator` owns signal handling and an optional exit timer, and exposes the exit result through its context mixin.
- `market_data_logger` logs the raw payloads relayed by the client.
- `binance_perp_client` owns one WebSocket and one subscription request; start/stop controls the connection.

Start passes through `app_terminator`, which arms the signal wait before forwarding to the client.
The client reports connection/failure events through `pass_response`. The terminator creates its
`nom::timer` only for a positive runtime limit after connection; on stop it cancels its waits and
forwards the stop command through `pass_request`. The terminator must precede every other
stop handler because `pass_request` starts after its originating node. Relay delivery is synchronous, so the client
checks whether the connection was closed before subscribing. `app` runs the context on one thread
and returns the exit result. Payload/source views are borrowed only during delivery, and the
context outlives all nodes.

## Validation

The local TLS integration checks verify one connection with multiple symbols and channels,
exact `trade` subscriptions, individual-trade payload logging, channel selection, ping/pong,
and disconnection cleanup. They use a local peer and do not establish exchange availability.
The `asio_comm_feeder_exit` suite checks zero/positive limits, delayed connection setup,
Ctrl+C/SIGTERM, cancellation during setup, failure exit codes and argument validation.
Zero/one-second limits and Ctrl+C after receiving trades were also verified against the public
endpoint on 2026-11-05; each intentional stop exited successfully.

See the [Spot feeder](../binance_spot_feeder/README.md) and [project overview](../../../README.md).
