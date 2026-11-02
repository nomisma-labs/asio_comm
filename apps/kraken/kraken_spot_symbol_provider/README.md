# kraken spot symbol provider

`kraken_spot_symbol_provider` uses `nom::web_socket` and Assembly/relay with C++ defaults in [config.h](config.h).

## Build and run

From the repository root, after installing the [build prerequisites](../../../docs/kit/build.md#toolchain):

```bash
cmake --preset dev
cmake --build --preset dev --target kraken_spot_symbol_provider
./build/dev/bin/kraken_spot_symbol_provider > symbols.json
./build/dev/bin/kraken_spot_symbol_provider --help
```

Default endpoint: `wss://ws.kraken.com/v2`. `--endpoint` overrides it for alternate regions or local TLS peers.
`--max-message-size BYTES` sets the incoming-message limit (default 64 MiB).

## API decisions

Subscribes to `instrument` with `snapshot:true` and tokenized assets disabled. Writes the first reference-data snapshot, including pairs and assets, then closes. Identifiers use V2 forms such as `BTC/USD`.

- [Official API reference 1](https://docs.kraken.com/exchange/api-reference/spot-websocket-v2/instrument)

## Assembly integration

[config.h](config.h) contains defaults, argument parsing and `app_config::validate()`. Validation returns
`std::expected<void, std::string>` and leaves the configuration unchanged. In [main.cpp](main.cpp),
`asio_service` owns the
I/O context, `kraken_spot_client` owns the WebSocket and its protocol callbacks, and `symbol_output`
consumes responses through `nom::relay<Traits>::pass_response`. `kraken_spot_assembly` combines these
nodes; `app` starts them through the relay and runs the single I/O thread. Any required heartbeat
or subscription timer belongs to the client node. Borrowed response views are consumed synchronously.

## Workflow and validation

Connect → request/subscribe → ignore acknowledgements → identify catalog → relay raw JSON to stdout → close.
The original response envelope is preserved. Basic string checks recognize the documented envelope and errors;
there is no JSON parser, DOM or normalized symbol model. Application diagnostics go to stderr.
Success exits 0; errors, missing catalogs and output failures exit 1.

The `asio_comm_exchange_apps` integration suite in the `complete` preset uses local TLS peers to check
wire requests, fragmented messages, heartbeat handling and shutdown. Production catalog retrieval was also verified on 2026-11-02.

[Applications overview](../../README.md)
