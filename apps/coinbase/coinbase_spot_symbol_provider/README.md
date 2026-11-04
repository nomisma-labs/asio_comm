# coinbase spot symbol provider

`coinbase_spot_symbol_provider` uses `nom::web_socket` and Assembly/relay with C++ defaults in [config.h](config.h).

## Build and run

From the repository root, after installing the [build prerequisites](../../../docs/kit/build.md#toolchain):

```bash
cmake --preset dev
cmake --build --preset dev --target coinbase_spot_symbol_provider
./build/dev/bin/coinbase_spot_symbol_provider > symbols.json
./build/dev/bin/coinbase_spot_symbol_provider --exit-after-sec 10 > symbols.json
./build/dev/bin/coinbase_spot_symbol_provider --help
```

Default endpoint: `wss://ws-feed.exchange.coinbase.com`. `--endpoint` overrides it for alternate regions or local TLS peers.
`--max-message-size BYTES` sets the incoming-message limit (default 64 MiB).

## Runtime and shutdown

`--exit-after-sec SECONDS` has the same timing as the feeders: omitted means no additional
application limit, positive values count from connection (TLS for HTTPS; WebSocket handshake for
WS), and `0` closes immediately after connection before requesting a catalog. Existing transport
connection/request deadlines still apply; this limit excludes connection setup.

Ctrl+C (`SIGINT`) and `SIGTERM` stop during setup or retrieval. A provider exits **0** only after
writing a complete catalog. Timeout, cancellation (including `--exit-after-sec 0`), connection/API
errors and output failure exit **1**. Stops close the transport, cancel the exit timer and signal
wait, drain pending callbacks and flush the logger. Stop diagnostics go to stderr. Timer and signal
handlers share the I/O thread, so synchronous output can delay their execution.

The shared `app_terminator` precedes the output/client nodes. Clients relay `connected`/`failed`;
`symbol_output` relays `completed` after writing the catalog. The provider config sets
`require_completion = true`, preserving the one-shot success contract with the shared stop logic.
The `asio_comm_provider_exit` local TLS suite covers these paths for every provider.

## API decisions

Subscribes to the public `status` channel, ignores the subscription acknowledgement and writes the first complete products/currencies catalog. The stream is periodic, but the app closes after one catalog.

- [Official API reference 1](https://docs.cdp.coinbase.com/exchange/websocket-feed/channels#status-channel)

## Assembly integration

The shared I/O node, terminator and lifecycle events are defined in [common_app_nodes.h](../../common_app_nodes.h).

[config.h](config.h) contains defaults, argument parsing and `app_config::validate()`. Validation returns
`std::expected<void, std::string>` and leaves the configuration unchanged. In [main.cpp](main.cpp),
`asio_service` owns the I/O context, and `app_terminator` owns signals, the optional exit timer and the exit result.
`coinbase_spot_client` owns the WebSocket and its protocol callbacks, and `symbol_output`
consumes responses through `nom::relay<Traits>::pass_response`. `coinbase_spot_assembly` combines these
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
