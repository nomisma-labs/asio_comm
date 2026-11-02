# Bitflyer Spot feeder

`bitflyer_spot_feeder` logs public market data using `nom::web_socket` and `spd_logger`.
Defaults are `BTC_JPY` with `ticker`, `trade`, and `depth`.

## Build and run

Follow the [build prerequisites](../../../docs/kit/build.md#toolchain), then run from the repository root:

```bash
cmake --preset dev
cmake --build --preset dev --target bitflyer_spot_feeder
./build/dev/bin/bitflyer_spot_feeder
./build/dev/bin/bitflyer_spot_feeder --symbols BTC_JPY,ETH_JPY --channels trade,depth
./build/dev/bin/bitflyer_spot_feeder --help
```

The app runs until Ctrl+C or disconnection. Connection failures and disconnections exit with
status 1; there is no automatic reconnect.

## Configuration and subscriptions

Settings, including `nom::log_config`, live in [config.h](config.h). `--symbols` and `--channels`
accept comma-separated lists. Symbols are normalized to uppercase; channel names are case-sensitive.
`--endpoint` overrides `wss://ws.lightstream.bitflyer.com/json-rpc`, including for local TLS tests.
No configuration file or credentials are needed.

| Channel option | Subscription |
| --- | --- |
| `ticker` | `lightning_ticker_<symbol>` |
| `trade` | `lightning_executions_<symbol>` |
| `depth` | `lightning_board_snapshot_<symbol>` and `lightning_board_<symbol>` |

Each channel gets a separate JSON-RPC `subscribe` request. Depth selects both snapshots and
incremental board updates. WebSocket pings are enabled to keep quiet connections active.
See the [public API](https://bf-lightning-api.readme.io/docs/endpoint-json-rpc) for the protocol.

Every incoming payload is logged unchanged, including acknowledgements and API errors. The app
does not interpret subscription success, reconstruct an order book, or check sequence numbers.
Callbacks and client calls run on one `io_context` thread. The synchronous logger consumes each
borrowed payload during its callback. See the [WebSocket contract](../../../docs/web_socket.md#usage-constraints-and-deferred-work).

## Assembly structure

`bitflyer_spot_assembly` in [main.cpp](main.cpp) composes three nodes:

- `asio_service` contributes the shared `io_context` through its `context_mixin`.
- `market_data_logger` handles `nom::event::market_data` responses and logs the payload.
- `bitflyer_spot_client` handles `nom::event::service_command` requests, owns the WebSocket
  connections, and adapts WebSocket callbacks into market-data responses.

`app` derives from `nom::relay<bitflyer_spot_assembly>`. Its `start()` sends the start command
through the relay and runs the context. The client sends each received payload back through
`pass_response()` to the logger. Stop commands close the connections.

Relay delivery is synchronous on the same thread as the WebSocket callbacks. Market-data events
borrow their source and payload views only for that call; no payload copy or queue is introduced.
The assembly context outlives its nodes, and the app stays alive while the context runs.

## Validation

The `asio_comm_exchange_feeders` integration test uses local TLS peers to check subscriptions,
multiple symbols, raw logging, keepalives and disconnect cleanup. It takes about 20 seconds to
exercise the real keepalive interval and is excluded by the `dev` test preset.

[Project overview](../../../README.md)
