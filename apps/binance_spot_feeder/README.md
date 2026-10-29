# Binance Spot feeder

`binance_spot_feeder` subscribes to public Binance Spot market-data streams and logs each
incoming payload through `spd_logger`. It uses `nom::web_socket` and requires no API key.

## Build and run

Follow the shared [build prerequisites](../../docs/kit/build.md#toolchain). From the repository root:

```bash
cmake --preset dev
cmake --build --preset dev --target binance_spot_feeder
./build/dev/bin/binance_spot_feeder
```

The defaults select `BTCUSDT` and all three channels. Override the selection with comma-separated lists:

```bash
./build/dev/bin/binance_spot_feeder --symbols BTCUSDT,ETHUSDT --channels trade,depth
./build/dev/bin/binance_spot_feeder --symbols BTCUSDT --channels ticker
./build/dev/bin/binance_spot_feeder --help
```

The app runs until interrupted with Ctrl+C or disconnected. Connection failures and disconnects
exit with status 1; there is no automatic reconnect. A 30-second deadline covers connection setup.

## Configuration and streams

Defaults live in `app_config` in [main.cpp](main.cpp), including `nom::log_config`.
`--symbols`, `--channels`, and `--endpoint` override the corresponding C++ settings.
Symbols accept ASCII letters and digits and are converted to lowercase for subscriptions.
Channel names are case-sensitive. No configuration file or JSON configuration library is used.

The default endpoint is `wss://stream.binance.com:443/ws`. On connection, the app sends one
`SUBSCRIBE` request containing every selected symbol/channel combination:

```json
{"method":"SUBSCRIBE","params":["btcusdt@ticker","btcusdt@trade","btcusdt@depth"],"id":1}
```

| Channel | Stream | Data |
| --- | --- | --- |
| `ticker` | `<symbol>@ticker` | Rolling 24-hour statistics, updated every second |
| `trade` | `<symbol>@trade` | Individual trades as they occur |
| `depth` | `<symbol>@depth` | Incremental order-book updates at the default one-second interval |

These names, intervals, and the subscription format follow the
[Binance Spot stream specification](https://github.com/binance/binance-spot-api-docs/blob/master/web-socket-streams.md).
Binance limits a connection to 1024 streams and disconnects it after 24 hours. The WebSocket
implementation answers server ping frames while reading; the feeder leaves outbound pings disabled.

Payloads are logged unchanged, including subscription acknowledgements and API errors. The app
does not parse messages, confirm subscription success, store data separately, or reconstruct an
order book from depth updates. See the [WebSocket constraints](../../docs/web_socket.md#usage-constraints-and-deferred-work)
for the client's current execution and lifetime contract.

## Assembly structure

`binance_spot_assembly` in [main.cpp](main.cpp) composes three nodes:

- `asio_service` contributes the shared `io_context` through its `context_mixin`.
- `market_data_logger` handles `nom::event::market_data` responses and logs the payload.
- `binance_spot_client` handles `nom::event::service_command` requests, owns the WebSocket
  connections and timers, and adapts WebSocket callbacks into market-data responses.

`app` derives from `nom::relay<binance_spot_assembly>`. Its `start()` sends the start command
through the relay and runs the context. The client sends each received payload back through
`pass_response()` to the logger. Stop commands cancel timers and close the connections.

Relay delivery is synchronous on the same thread as the WebSocket callbacks. Market-data events
borrow their source and payload views only for that call; no payload copy or queue is introduced.
The assembly context outlives its nodes, and the app stays alive while the context runs.

## Validation

The local TLS integration test checks default and multi-symbol subscriptions, channel selection,
logging of several messages, ping/pong handling, and invalid configuration. It needs no Binance access.

See the [project overview](../../README.md) for dependencies and other examples.
