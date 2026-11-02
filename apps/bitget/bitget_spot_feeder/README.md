# bitget spot feeder

`bitget_spot_feeder` uses `nom::web_socket` and Assembly/relay with C++ defaults in [config.h](config.h).

## Build and run

From the repository root, after installing the [build prerequisites](../../../docs/kit/build.md#toolchain):

```bash
cmake --preset dev
cmake --build --preset dev --target bitget_spot_feeder
./build/dev/bin/bitget_spot_feeder
./build/dev/bin/bitget_spot_feeder --help
```

Default endpoint: `wss://ws.bitget.com/v3/ws/public`. `--endpoint` overrides it for alternate regions or local TLS peers.
`--max-message-size BYTES` sets the incoming-message limit (default 16 MiB).
`--symbols A,B` and `--channels ticker,trade,depth` select the streams; the source comments document limits.

## API decisions

Uses the current V3 public endpoint with uppercase `BTCUSDT`. Requests use `instType:spot`, `topic` and `symbol`. Channels map to `ticker` (200 ms), `publicTrade` and `books` (initial snapshot then 50 ms updates). A single batched command is limited to 100 selected pairs and a 4096-byte args budget in this example.

- [Official API reference 1](https://www.bitget.com/docs/uta/websocket/public/Tickers-Channel)
- [Official API reference 2](https://www.bitget.com/docs/uta/websocket/public/New-Trades-Channel)
- [Official API reference 3](https://www.bitget.com/docs/uta/websocket/public/Order-Book-Channel)
- [Official API reference 4](https://www.bitget.com/docs/uta/quick-start)

## Assembly integration

[config.h](config.h) contains defaults, argument parsing and `app_config::validate()`. Validation returns
`std::expected<void, std::string>` and leaves the configuration unchanged. In [main.cpp](main.cpp),
`asio_service` owns the
I/O context, `bitget_spot_client` owns the WebSocket and its protocol callbacks, and `market_data_logger`
consumes responses through `nom::relay<Traits>::pass_response`. `bitget_spot_assembly` combines these
nodes; `app` starts them through the relay and runs the single I/O thread. Any required heartbeat
or subscription timer belongs to the client node. Borrowed response views are consumed synchronously.

## Workflow and validation

Connect → send subscriptions → log acknowledgements and updates → maintain the documented heartbeat.
The app exits on Ctrl+C or disconnection, with status 1; it does not reconnect, reconstruct books or verify
sequence continuity. The single io_context thread also runs synchronous relay/log delivery.

The `asio_comm_exchange_apps` integration suite in the `complete` preset uses local TLS peers to check
wire requests, fragmented messages, heartbeat handling and shutdown. A short public production subscription was verified on 2026-11-02.

[Applications overview](../../README.md)
