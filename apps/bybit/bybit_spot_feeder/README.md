# bybit spot feeder

`bybit_spot_feeder` uses `nom::web_socket` and Assembly/relay with C++ defaults in [config.h](config.h).

## Build and run

From the repository root, after installing the [build prerequisites](../../../docs/kit/build.md#toolchain):

```bash
cmake --preset dev
cmake --build --preset dev --target bybit_spot_feeder
./build/dev/bin/bybit_spot_feeder
./build/dev/bin/bybit_spot_feeder --help
```

Default endpoint: `wss://stream.bybit.com/v5/public/spot`. `--endpoint` overrides it for alternate regions or local TLS peers.
`--max-message-size BYTES` sets the incoming-message limit (default 16 MiB).
`--symbols A,B` and `--channels ticker,trade,depth` select the streams; the source comments document limits.

## API decisions

Uses V5 Spot with `BTCUSDT`. `ticker` selects both `tickers` and the `orderbook.1` BBO stream; `trade` selects `publicTrade`; `depth` selects `orderbook.50`. The book sends snapshots and deltas. Identifiers become uppercase. The ten-argument request limit includes the extra BBO topic.

- [Official API reference 1](https://bybit-exchange.github.io/docs/v5/websocket/public/orderbook)
- [Official API reference 2](https://bybit-exchange.github.io/docs/v5/ws/connect)

## Assembly integration

[config.h](config.h) contains defaults, argument parsing and `app_config::validate()`. Validation returns
`std::expected<void, std::string>` and leaves the configuration unchanged. In [main.cpp](main.cpp),
`asio_service` owns the
I/O context, `bybit_spot_client` owns the WebSocket and its protocol callbacks, and `market_data_logger`
consumes responses through `nom::relay<Traits>::pass_response`. `bybit_spot_assembly` combines these
nodes; `app` starts them through the relay and runs the single I/O thread. Any required heartbeat
or subscription timer belongs to the client node. Borrowed response views are consumed synchronously.

## Workflow and validation

Connect → send subscriptions → log acknowledgements and updates → maintain the documented heartbeat.
The app exits on Ctrl+C or disconnection, with status 1; it does not reconnect, reconstruct books or verify
sequence continuity. The single io_context thread also runs synchronous relay/log delivery.

The `asio_comm_exchange_apps` integration suite in the `complete` preset uses local TLS peers to check
wire requests, fragmented messages, heartbeat handling and shutdown. A short public production subscription was verified on 2026-11-02.

[Applications overview](../../README.md)
