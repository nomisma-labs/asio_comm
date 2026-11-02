# binance spot sbe feeder

`binance_spot_sbe_feeder` uses `nom::web_socket` and Assembly/relay with C++ defaults in [config.h](config.h).

## Build and run

From the repository root, after installing the [build prerequisites](../../../docs/kit/build.md#toolchain):

```bash
cmake --preset dev
cmake --build --preset dev --target binance_spot_sbe_feeder
export BINANCE_SBE_API_KEY=YOUR_ED25519_API_KEY
./build/dev/bin/binance_spot_sbe_feeder
./build/dev/bin/binance_spot_sbe_feeder --help
```

Default endpoint: `wss://stream-sbe.binance.com/ws`. `--endpoint` overrides it for alternate regions or local TLS peers.
`--max-message-size BYTES` sets the incoming-message limit (default 16 MiB).
`--symbols A,B` and `--channels ticker,trade,depth` select the streams; the source comments document limits.

## API decisions

Uses lowercase wire symbols and channels `bestBidAsk`, `trade` and `depth` (20 ms). Subscription commands/acks are JSON; market data is SBE. Logs only the template name/ID and message size. An eight-byte little-endian header is inspected; fields in the market body are not decoded. Unknown/truncated templates are labeled safely.

The API key is sent only in `X-MBX-APIKEY`. No private key, signature or trading permission is needed.

- [Official API reference 1](https://github.com/binance/binance-spot-api-docs/blob/master/sbe-market-data-streams.md)
- [Official API reference 2](https://github.com/binance/binance-spot-api-docs/blob/master/sbe/schemas/stream_1_0.xml)

## Assembly integration

[config.h](config.h) contains defaults, argument parsing and `app_config::validate()`. Validation returns
`std::expected<void, std::string>` and leaves the configuration unchanged. In [main.cpp](main.cpp),
`asio_service` owns the
I/O context, `binance_spot_sbe_client` owns the WebSocket and its protocol callbacks, and `market_data_logger`
consumes responses through `nom::relay<Traits>::pass_response`. `binance_spot_sbe_assembly` combines these
nodes; `app` starts them through the relay and runs the single I/O thread. Any required heartbeat
or subscription timer belongs to the client node. Borrowed response views are consumed synchronously.

## Workflow and validation

Connect → send subscriptions → log acknowledgements and updates → maintain the documented heartbeat.
The app exits on Ctrl+C or disconnection, with status 1; it does not reconnect, reconstruct books or verify
sequence continuity. The single io_context thread also runs synchronous relay/log delivery.

The `asio_comm_exchange_apps` integration suite in the `complete` preset uses local TLS peers to check
wire requests, fragmented messages, heartbeat handling and shutdown. Production SBE access remains unverified without an API key; local tests use a dummy header and SBE fixtures.

[Applications overview](../../README.md)
