# OKX Spot feeder

`okex_spot_feeder` logs public market data using `nom::web_socket` and `spd_logger`.
Defaults are `BTC-USDT` with `ticker`, `trade`, and `depth`.

## Build and run

Follow the [build prerequisites](../../../docs/kit/build.md#toolchain), then run from the repository root:

```bash
cmake --preset dev
cmake --build --preset dev --target okex_spot_feeder
./build/dev/bin/okex_spot_feeder
./build/dev/bin/okex_spot_feeder --symbols BTC-USDT,ETH-USDT --channels trade,depth
./build/dev/bin/okex_spot_feeder --help
```

The app runs until Ctrl+C or disconnection. Connection failures and disconnections exit with
status 1; there is no automatic reconnect.

## Configuration and subscriptions

Settings, including `nom::log_config`, live in [config.h](config.h). `--symbols` and `--channels`
accept comma-separated lists. Symbols are normalized to uppercase; channel names are case-sensitive.
`--endpoint` overrides `wss://ws.okx.com/ws/v5/public`, including for local TLS tests.
No configuration file or credentials are needed.

| Channel option | Subscription |
| --- | --- |
| `ticker` | `tickers` |
| `trade` | `trades` |
| `depth` | `books` |

One `subscribe` request contains each selected channel and `instId`. `books` supplies an
initial 400-level snapshot followed by incremental updates. Symbols use `BASE-QUOTE` syntax.
The endpoint uses port 443. The executable keeps the requested `okex` name.

An application timer sends text `ping` every 20 seconds. Text `pong` replies are logged;
they do not establish market-data freshness. The ping timer is canceled when the feed ends.
See the [OKX API guide](https://www.okx.com/docs-v5/en/#overview-websocket).

Every incoming payload is logged unchanged, including acknowledgements and API errors. The app
does not interpret subscription success, reconstruct an order book, or check sequence numbers.
Callbacks and client calls run on one `io_context` thread. The synchronous logger consumes each
borrowed payload during its callback. See the [WebSocket contract](../../../docs/asio_comm/web_socket.md#usage-constraints-and-deferred-work).

## Assembly structure

`okex_spot_assembly` in [main.cpp](main.cpp) composes three nodes:

- `asio_service` contributes the shared `io_context` through its `context_mixin`.
- `market_data_logger` handles `nom::event::market_data` responses and logs the payload.
- `okex_spot_client` handles `nom::event::service_command` requests, owns the WebSocket
  connections and timers, and adapts WebSocket callbacks into market-data responses.

`app` derives from `nom::relay<okex_spot_assembly>`. Its `start()` sends the start command
through the relay and runs the context. The client sends each received payload back through
`pass_response()` to the logger. Stop commands cancel timers and close the connections.

Relay delivery is synchronous on the same thread as the WebSocket callbacks. Market-data events
borrow their source and payload views only for that call; no payload copy or queue is introduced.
The assembly context outlives its nodes, and the app stays alive while the context runs.

## Validation

The `asio_comm_exchange_feeders` integration test uses local TLS peers to check subscriptions,
multiple symbols, raw logging, keepalives and disconnect cleanup. It takes about 20 seconds to
exercise the real keepalive interval and is excluded by the `dev` test preset.

[Project overview](../../../README.md)
