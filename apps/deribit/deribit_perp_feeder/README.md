# Deribit perpetual futures feeder

`deribit_perp_feeder` logs public market data using `nom::web_socket` and `spd_logger`.
Defaults are `BTC-PERPETUAL` with `ticker`, `trade`, and `depth`.

## Build and run

Follow the [build prerequisites](../../../docs/kit/build.md#toolchain), then run from the repository root:

```bash
cmake --preset dev
cmake --build --preset dev --target deribit_perp_feeder
./build/dev/bin/deribit_perp_feeder
./build/dev/bin/deribit_perp_feeder --exit-after-sec 10
./build/dev/bin/deribit_perp_feeder --symbols BTC-PERPETUAL,ETH-PERPETUAL --channels trade,depth
./build/dev/bin/deribit_perp_feeder --help
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

## Configuration and subscriptions

Settings, including `nom::log_config`, live in [config.h](config.h). `--symbols` and `--channels`
accept comma-separated lists. Symbols are normalized to uppercase; channel names are case-sensitive.
`--endpoint` overrides `wss://www.deribit.com/ws/api/v2`, including for local TLS tests.
No configuration file or credentials are needed.

| Channel option | Subscription |
| --- | --- |
| `ticker` | `ticker.<symbol>.100ms` |
| `trade` | `trades.<symbol>.100ms` |
| `depth` | `book.<symbol>.100ms` |

One JSON-RPC `public/subscribe` request selects all channels. Updates use the public `100ms`
interval; `raw` streams require authentication. The default `BTC-PERPETUAL` is an inverse
perpetual contract. Symbols must end in `-PERPETUAL`; at most 500 subscriptions are sent.
WebSocket pings keep quiet connections active; JSON-RPC heartbeat requests are not enabled.
See Deribit's [subscription example](https://docs.deribit.com/articles/deribit-quickstart)
and [book channel](https://docs.deribit.com/subscriptions/orderbook/bookinstrument_nameinterval).

Every incoming payload is logged unchanged, including acknowledgements and API errors. The app
does not interpret subscription success, reconstruct an order book, or check sequence numbers.
Callbacks and client calls run on one `io_context` thread. The synchronous logger consumes each
borrowed payload during its callback. See the [WebSocket contract](../../../docs/asio_comm/web_socket.md#usage-constraints-and-deferred-work).

## Assembly structure

The I/O node, terminator and lifecycle events are shared in [common_app_nodes.h](../../common_app_nodes.h).
Exchange nodes and assembly composition remain in [main.cpp](main.cpp).

`deribit_perp_assembly` in [main.cpp](main.cpp) composes four nodes:

- `asio_service` contributes the shared `io_context` through its `context_mixin`.
- `app_terminator` owns signals and an optional exit timer; its context mixin exposes the exit result.
- `market_data_logger` handles `nom::event::market_data` responses and logs the payload.
- `deribit_perp_client` handles `nom::event::service_command` requests, owns one WebSocket
  connection, and adapts WebSocket callbacks into market-data responses.

`app` derives from `nom::relay<deribit_perp_assembly>`. Its `start()` sends the start command
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

The `asio_comm_exchange_feeders` integration test uses local TLS peers to check subscriptions,
multiple symbols, raw logging, keepalives and disconnect cleanup. It takes about 20 seconds to
exercise the real keepalive interval and is excluded by the `dev` test preset.

The `asio_comm_feeder_exit` local TLS suite checks zero/positive limits, delayed connection setup,
Ctrl+C/SIGTERM, cancellation during setup, failure exits and invalid runtime arguments.

[Project overview](../../../README.md)
