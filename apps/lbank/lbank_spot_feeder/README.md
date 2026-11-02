# lbank spot feeder

`lbank_spot_feeder` uses `nom::web_socket` and Assembly/relay with C++ defaults in [config.h](config.h).

## Build and run

From the repository root, after installing the [build prerequisites](../../../docs/kit/build.md#toolchain):

```bash
cmake --preset dev
cmake --build --preset dev --target lbank_spot_feeder
./build/dev/bin/lbank_spot_feeder
./build/dev/bin/lbank_spot_feeder --help
```

Default endpoint: `wss://api.lbank.info/ws/V2/`. `--endpoint` overrides it for alternate regions or local TLS peers.
`--max-message-size BYTES` sets the incoming-message limit (default 16 MiB).
`--symbols A,B` and `--channels ticker,trade,depth` select the streams; the source comments document limits.

## API decisions

Uses lowercase `btc_usdt`. Channels map to `tick` (summary), `trade` and depth 100. Each subscription is spaced by 200 ms. Application heartbeat replies preserve the server ping token exactly. The summary channel does not itself provide a BBO feed; depth contains the available book.

- [Official API reference 1](https://www.lbank.com/docs/index.html#websocket-api-market-data)

## Assembly integration

[config.h](config.h) contains defaults, argument parsing and `app_config::validate()`. Validation returns
`std::expected<void, std::string>` and leaves the configuration unchanged. In [main.cpp](main.cpp),
`asio_service` owns the
I/O context, `lbank_spot_client` owns the WebSocket and its protocol callbacks, and `market_data_logger`
consumes responses through `nom::relay<Traits>::pass_response`. `lbank_spot_assembly` combines these
nodes; `app` starts them through the relay and runs the single I/O thread. Any required heartbeat
or subscription timer belongs to the client node. Borrowed response views are consumed synchronously.

## Workflow and validation

Connect → send subscriptions → log acknowledgements and updates → maintain the documented heartbeat.
The app exits on Ctrl+C or disconnection, with status 1; it does not reconnect, reconstruct books or verify
sequence continuity. The single io_context thread also runs synchronous relay/log delivery.

The `asio_comm_exchange_apps` integration suite in the `complete` preset uses local TLS peers to check
wire requests, fragmented messages, heartbeat handling and shutdown. A short public production subscription was verified on 2026-11-02.

[Applications overview](../../README.md)
