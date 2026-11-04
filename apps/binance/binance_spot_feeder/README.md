# Binance Spot feeder

`binance_spot_feeder` subscribes to public Binance Spot market-data streams and logs each
incoming payload through `spd_logger`. It uses `nom::web_socket` and requires no API key.

## Build and run

Follow the shared [build prerequisites](../../../docs/kit/build.md#toolchain). From the repository root:

```bash
cmake --preset dev
cmake --build --preset dev --target binance_spot_feeder
./build/dev/bin/binance_spot_feeder
./build/dev/bin/binance_spot_feeder --exit-after-sec 10
```

The defaults select `BTCUSDT` with `bookTicker`, `trade`, and `depth@100ms`. Override the selection with comma-separated lists:

```bash
./build/dev/bin/binance_spot_feeder --symbols BTCUSDT,ETHUSDT --channels trade,depth@100ms
./build/dev/bin/binance_spot_feeder --symbols BTCUSDT --channels bookTicker
./build/dev/bin/binance_spot_feeder --help
```

## Runtime and shutdown

`--exit-after-sec SECONDS` accepts unsigned whole seconds; omission means no runtime limit.
Positive values count from the completed WebSocket handshake, independently of data; `0` closes
immediately after connection, before subscriptions or heartbeat scheduling. Connection setup has
its own WebSocket deadline and is excluded from this limit.

Ctrl+C (`SIGINT`) and `SIGTERM` also work during setup. Signals and runtime expiry exit **0**;
connection failures, unexpected disconnection and invalid arguments exit **1**. Stop cancels timers
and signal waits, closes the transport through `web_socket::close()`, drains pending callbacks and
flushes logs. It does not wait for a WebSocket closing handshake. There is no automatic reconnect.

## Configuration and streams

Defaults live in `app_config` in [config.h](config.h), including `nom::log_config`.
`--symbols`, `--channels`, `--endpoint`, and `--exit-after-sec` override the corresponding C++ settings.
Symbols accept ASCII letters and digits and are converted to lowercase for subscriptions.
Channel names are case-sensitive and sent unchanged; supported values are `bookTicker`,
`trade`, `depth`, and `depth@100ms`. No configuration file or JSON configuration library is used.

The default endpoint is `wss://stream.binance.com:443/ws`. On connection, the app sends one
`SUBSCRIBE` request containing every selected symbol/channel combination:

```json
{"method":"SUBSCRIBE","params":["btcusdt@bookTicker","btcusdt@trade","btcusdt@depth@100ms"],"id":1}
```

| Channel | Stream | Data |
| --- | --- | --- |
| `bookTicker` | `<symbol>@bookTicker` | Best bid/ask price and quantity updates |
| `trade` | `<symbol>@trade` | Individual trades as they occur |
| `depth@100ms` | `<symbol>@depth@100ms` | Incremental order-book updates at the requested 100 ms interval |
| `depth` | `<symbol>@depth` | Incremental updates at the server's default interval |

These names, intervals, and the subscription format follow the
[Binance Spot stream specification](https://github.com/binance/binance-spot-api-docs/blob/master/web-socket-streams.md).
Binance limits a connection to 1024 streams and disconnects it after 24 hours. The WebSocket
implementation answers server ping frames while reading; the feeder leaves outbound pings disabled.

Payloads are logged unchanged, including subscription acknowledgements and API errors. The app
does not use a JSON parser, confirm subscription success, store data separately, or reconstruct
an order book from depth updates. See the [WebSocket constraints](../../../docs/asio_comm/web_socket.md#usage-constraints-and-deferred-work)
for the client's current execution and lifetime contract.

## Assembly structure

The I/O node, terminator and lifecycle events are shared in [common_app_nodes.h](../../common_app_nodes.h).
Exchange nodes and assembly composition remain in [main.cpp](main.cpp).

`binance_spot_assembly` in [main.cpp](main.cpp) composes four nodes:

- `asio_service` contributes the shared `io_context` through its `context_mixin`.
- `app_terminator` owns signals and an optional exit timer; its context mixin exposes the exit result.
- `market_data_logger` handles `nom::event::market_data` responses and logs the payload.
- `binance_spot_client` handles `nom::event::service_command` requests, owns one WebSocket and one subscription request, and adapts WebSocket callbacks into market-data responses.

`app` derives from `nom::relay<binance_spot_assembly>`. Its `start()` sends the start command
through the relay and runs the context. The client sends each received payload back through
`pass_response()` to the logger. Stop commands cancel client timers and close the connection.

The client relays `feed_event::connected` and `feed_event::failed` to `app_terminator` through
`pass_response`. The terminator fixes the exit result on the first stop and sends stop through
`pass_request`; it must precede all other stop handlers because this starts after the emitting node.
The client retains responsibility for its WebSocket and any exchange timers. `app` runs the context
until all callbacks drain and returns the result exposed by the terminator's context mixin.

Relay delivery is synchronous on the same thread as the WebSocket callbacks. Market-data events
borrow their source and payload views only for that call; no payload copy or queue is introduced.
The assembly context outlives its nodes, and the app stays alive while the context runs.

## Validation

The local TLS integration test checks default and multi-symbol subscriptions, channel selection,
logging of several messages, ping/pong handling, and invalid configuration. It needs no Binance access.

For receive-callback subscription checks, use the
[Binance Spot resubscription test](../binance_spot_resubscribe_test/README.md).

The `asio_comm_feeder_exit` local TLS suite checks zero/positive limits, delayed connection setup,
Ctrl+C/SIGTERM, cancellation during setup, failure exits and invalid runtime arguments.

See the [project overview](../../../README.md) for dependencies and other examples.
