# Deribit perpetual futures feeder

`deribit_perp_feeder` logs public market data using `nom::web_socket` and `spd_logger`.
Defaults are `BTC-PERPETUAL` with `ticker`, `trade`, and `depth`.

## Build and run

Follow the [build prerequisites](../../docs/kit/build.md#toolchain), then run from the repository root:

```bash
cmake --preset dev
cmake --build --preset dev --target deribit_perp_feeder
./build/dev/bin/deribit_perp_feeder
./build/dev/bin/deribit_perp_feeder --symbols BTC-PERPETUAL,ETH-PERPETUAL --channels trade,depth
./build/dev/bin/deribit_perp_feeder --help
```

The app runs until Ctrl+C or disconnection. Connection failures and disconnections exit with
status 1; there is no automatic reconnect. Connection setup has a 30-second deadline.

## Configuration and subscriptions

Settings, including `nom::log_config`, live in [main.cpp](main.cpp). `--symbols` and `--channels`
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
borrowed payload during its callback. See the [WebSocket contract](../../docs/web_socket.md#usage-constraints-and-deferred-work).

## Validation

The `asio_comm_exchange_feeders` integration test uses local TLS peers to check subscriptions,
multiple symbols, raw logging, keepalives and disconnect cleanup. It takes about 20 seconds to
exercise the real keepalive interval and is excluded by the `dev` test preset.

[Project overview](../../README.md)
