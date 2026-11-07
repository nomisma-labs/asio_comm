# Binance Spot resubscription test

`binance_spot_resubscribe_test` exercises sending from a WebSocket receive callback. It starts
with BTCUSDT trades, then adds ETHUSDT after three market-data messages on the same connection.
It uses `nom::web_socket`, Assembly/relay, and raw payload logging. No API key is needed.

## Build and run

```bash
cmake --preset dev
cmake --build --preset dev --target binance_spot_resubscribe_test
./build/dev/bin/binance_spot_resubscribe_test
```

Defaults live in `app_config` in [config.h](config.h): endpoint
`wss://stream.binance.com:443/ws`, initial symbol `BTCUSDT`, channel `trade`, additional symbol
`ETHUSDT`, a threshold of three market-data messages, and `nom::log_config`.

Override the selections or threshold independently:

```bash
./build/dev/bin/binance_spot_resubscribe_test --resubscribe-after 100
./build/dev/bin/binance_spot_resubscribe_test --symbols BTCUSDT --channels bookTicker,trade \
  --resubscribe-after 10 --resubscribe-symbols ETHUSDT,SOLUSDT
./build/dev/bin/binance_spot_resubscribe_test --help
```

`--endpoint` selects a different TLS endpoint, including a local test peer. The threshold must
be a positive `unsigned`. Symbols accept ASCII letters/digits and are normalized to lowercase.
Channels support `bookTicker`, `trade`, `depth`, and `depth@100ms`. The additional symbols use
the initial channel selection; the combined selection must fit the 1024-stream connection limit.

## What the tool exercises

The initial `SUBSCRIBE` uses ID 1. The Nth market-data callback sends a second `SUBSCRIBE`
with ID 2, before that callback returns. It adds the selected symbols once, retaining the
original streams on the same connection. The send is synchronous; no posting, send queue,
or send gate is introduced by this tool.

Counting uses a basic search for the `"s"` symbol field present in the supported market-data
payloads. Subscription acknowledgements and control frames do not count. There is no JSON
parser or schema validation. Every received payload is routed synchronously through the relay
to `market_data_logger`; the event views are borrowed for that call only.

`Resubscription send returned true` means the local blocking write completed. Inspect the raw
`{"result":null,"id":2}` acknowledgement and subsequent additional-symbol data to establish
server acceptance and continued reads. These request/reply conventions follow the
[Binance Spot stream specification](https://github.com/binance/binance-spot-api-docs/blob/master/web-socket-streams.md#live-subscribingunsubscribing-to-streams).

This is a continuous diagnostic tool: it keeps logging after the additional subscription until
interrupted or disconnected. Connection failures and disconnects exit with status 1; the tool
does not turn log inspection into an automatic pass verdict. There is no reconnect or application
deadline. The I/O node and service commands come from [common_app_nodes.h](../../common_app_nodes.h).
`asio_service` owns the shared context, `resubscribe_client` owns the connection and
counter, and `app` starts and runs the relay.

## Automated validation

`asio_comm_binance_spot_resubscribe` uses a local TLS peer to verify the default run and
N=1/3/100, fragmented input, interleaved pings, acknowledgement exclusion, exact request timing,
one connection, and continued data for both symbols. It also checks arguments and help output.
The harness bounds execution and judges the result; it needs no Binance access.

The [WS-03 investigation](../../../src/asio_comm/ws_current_work.md#ws-03) is closed as unconfirmed
and records the passing baseline and conditions for reopening. These tests do not establish
that every possible operation overlap is safe.
For continuous subscriptions without a transition, use the
[Binance Spot feeder](../binance_spot_feeder/README.md).
