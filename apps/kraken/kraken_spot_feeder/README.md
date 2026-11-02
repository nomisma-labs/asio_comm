# kraken spot feeder

`kraken_spot_feeder` uses `nom::web_socket` and Assembly/relay with C++ defaults in [config.h](config.h).

## Build and run

From the repository root, after installing the [build prerequisites](../../../docs/kit/build.md#toolchain):

```bash
cmake --preset dev
cmake --build --preset dev --target kraken_spot_feeder
./build/dev/bin/kraken_spot_feeder
./build/dev/bin/kraken_spot_feeder --help
```

Default endpoint: `wss://ws.kraken.com/v2`. `--endpoint` overrides it for alternate regions or local TLS peers.
`--max-message-size BYTES` sets the incoming-message limit (default 16 MiB).
`--symbols A,B` and `--channels ticker,trade,depth` select the streams; the source comments document limits.

## API decisions

Uses the public V2 API and uppercase `BTC/USD` identifiers. Ticker uses `event_trigger:bbo`; trades contain executions; depth requests a 100-level book snapshot followed by updates. One request per channel is spaced by 200 ms. V2 uses BTC rather than the V1 XBT spelling.

- [Official API reference 1](https://docs.kraken.com/exchange/api-reference/spot-websocket-v2/ticker)
- [Official API reference 2](https://docs.kraken.com/exchange/api-reference/spot-websocket-v2/book)
- [Official API reference 3](https://docs.kraken.com/exchange/api-reference/spot-websocket-v2/trade)

## Assembly integration

[config.h](config.h) contains defaults, argument parsing and `app_config::validate()`. Validation returns
`std::expected<void, std::string>` and leaves the configuration unchanged. In [main.cpp](main.cpp),
`asio_service` owns the
I/O context, `kraken_spot_client` owns the WebSocket and its protocol callbacks, and `market_data_logger`
consumes responses through `nom::relay<Traits>::pass_response`. `kraken_spot_assembly` combines these
nodes; `app` starts them through the relay and runs the single I/O thread. Any required heartbeat
or subscription timer belongs to the client node. Borrowed response views are consumed synchronously.

## Workflow and validation

Connect → send subscriptions → log acknowledgements and updates → maintain the documented heartbeat.
The app exits on Ctrl+C or disconnection, with status 1; it does not reconnect, reconstruct books or verify
sequence continuity. The single io_context thread also runs synchronous relay/log delivery.

The `asio_comm_exchange_apps` integration suite in the `complete` preset uses local TLS peers to check
wire requests, fragmented messages, heartbeat handling and shutdown. A short public production subscription was verified on 2026-11-02.

[Applications overview](../../README.md)
