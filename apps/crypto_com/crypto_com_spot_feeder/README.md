# crypto com spot feeder

`crypto_com_spot_feeder` uses `nom::web_socket` and Assembly/relay with C++ defaults in [config.h](config.h).

## Build and run

From the repository root, after installing the [build prerequisites](../../../docs/kit/build.md#toolchain):

```bash
cmake --preset dev
cmake --build --preset dev --target crypto_com_spot_feeder
./build/dev/bin/crypto_com_spot_feeder
./build/dev/bin/crypto_com_spot_feeder --exit-after-sec 10
./build/dev/bin/crypto_com_spot_feeder --help
```

Default endpoint: `wss://stream.crypto.com/exchange/v1/market`. `--endpoint` overrides it for alternate regions or local TLS peers.
`--max-message-size BYTES` sets the incoming-message limit (default 16 MiB).
`--symbols A,B` and `--channels ticker,trade,depth` select the streams; the source comments document limits.

## Runtime and shutdown

`--exit-after-sec SECONDS` accepts unsigned whole seconds; omission means no runtime limit.
Positive values count from the completed WebSocket handshake, independently of data; `0` closes
immediately after connection, before subscriptions or heartbeat scheduling. Connection setup has
its own WebSocket deadline and is excluded from this limit.

Ctrl+C (`SIGINT`) and `SIGTERM` also work during setup. Signals and runtime expiry exit **0**;
connection failures, unexpected disconnection and invalid arguments exit **1**. Stop cancels timers
and signal waits, closes the transport through `web_socket::close()`, drains pending callbacks and
flushes logs. It does not wait for a WebSocket closing handshake. There is no automatic reconnect.

## API decisions

Uses uppercase `BTC_USDT` on the public market endpoint. Channels select ticker/BBO, trades and ten-level books. The first subscription waits one second after connecting; all pairs share one command. The server heartbeat ID is echoed with `public/respond-heartbeat` within the receive callback.

- [Official API reference 1](https://exchange-developer.crypto.com/exchange/v1/docs/api/websocket-single-page/)

## Assembly integration

The I/O node, terminator and lifecycle events are shared in [common_app_nodes.h](../../common_app_nodes.h).
Exchange nodes and assembly composition remain in [main.cpp](main.cpp).

[config.h](config.h) contains defaults, argument parsing and `app_config::validate()`. Validation returns
`std::expected<void, std::string>` and leaves the configuration unchanged. In [main.cpp](main.cpp),
`asio_service` owns the I/O context, `app_terminator` owns signals and an optional exit timer,
`crypto_com_spot_client` owns the WebSocket and its protocol callbacks, and `market_data_logger`
consumes responses through `nom::relay<Traits>::pass_response`. `crypto_com_spot_assembly` combines these
nodes; `app` starts them through the relay and runs the single I/O thread. Any required heartbeat
or subscription timer belongs to the client node. Borrowed response views are consumed synchronously.

The client relays `feed_event::connected` and `feed_event::failed` to `app_terminator` through
`pass_response`. The terminator fixes the exit result on the first stop and sends stop through
`pass_request`; it must precede all other stop handlers because this starts after the emitting node.
The client retains responsibility for its WebSocket and any exchange timers. `app` runs the context
until all callbacks drain and returns the result exposed by the terminator's context mixin.

## Workflow and validation

Connect → send subscriptions → log acknowledgements and updates → maintain the documented heartbeat.
The app does not reconnect, reconstruct books or verify sequence continuity. The single I/O thread
also runs synchronous relay/log delivery.

The `asio_comm_exchange_apps` integration suite in the `complete` preset uses local TLS peers to check
wire requests, fragmented messages, heartbeat handling and shutdown. A short public production subscription was verified on 2026-11-02.

The `asio_comm_feeder_exit` local TLS suite checks zero/positive limits, delayed connection setup,
Ctrl+C/SIGTERM, cancellation during setup, failure exits and invalid runtime arguments.

[Applications overview](../../README.md)
