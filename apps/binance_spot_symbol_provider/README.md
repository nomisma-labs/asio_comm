# Binance Spot symbol provider

`binance_spot_symbol_provider` fetches public Spot trading rules and symbol metadata once over
WebSocket. It needs no API key. The app checks for API errors using basic string searches,
relays the original response to an output node, closes the connection, and exits with status 0.
Transport failures, detected API errors, missing/non-200 status, and output failures exit with status 1.

## Build and run

```bash
cmake --preset dev
cmake --build --preset dev --target binance_spot_symbol_provider
./build/dev/bin/binance_spot_symbol_provider > exchange_info.json
```

On success, stdout contains the complete response followed by a newline. A byte count goes
to stderr. Failure diagnostics may also appear in the logger's console output;
check the exit status before using the output file.

Defaults live in `app_config` in [main.cpp](main.cpp): the endpoint, a 64 MiB incoming-message
limit, and `nom::log_config`. The two command-line overrides are:

```bash
./build/dev/bin/binance_spot_symbol_provider --endpoint wss://ws-api.binance.com:9443/ws-api/v3
./build/dev/bin/binance_spot_symbol_provider --max-message-size 33554432
./build/dev/bin/binance_spot_symbol_provider --help
```

There is one connection, one request, and no reconnect or application deadline.
`--max-message-size` accepts a byte count in `[1, UINT_MAX]`.

## Request and response

The production endpoint is `wss://ws-api.binance.com:443/ws-api/v3`. On connection, send:

```json
{"id":"exchange-info","method":"exchangeInfo","params":{"permissions":["SPOT"],"showPermissionSets":false}}
```

This selects symbols with Spot permission, including non-trading statuses, and suppresses the
contents of `permissionSets` using `showPermissionSets:false`. Trading filters and the other
symbol metadata remain available. Endpoint, request, and response fields follow the
[Binance Spot WebSocket API specification](https://github.com/binance/binance-spot-api-docs/blob/master/web-socket-api.md#exchange-information).

String checks look for an `error` field and require a `status` token of `200`, allowing whitespace
around the colon and value. Error diagnostics include the original response. These checks assume
Binance's expected response envelope; they do not validate JSON syntax, nesting, request IDs,
or symbol contents. There is no JSON parser, DOM, symbol counting, or separate symbol model.
The original response envelope is forwarded unchanged, including exchange rules and rate-limit information.

The app sets an explicit 64 MiB message limit through `update_params()` before connecting;
the observed response with permission-set contents suppressed was about 6.71 MB. The socket
receive buffer stays at 64 KiB; its size does not limit the complete message. The message byte limit
does not bound total process memory.

## Assembly and validation

`asio_service` supplies the shared `io_context` and completion result. `binance_spot_client`
adapts WebSocket callbacks into `nom::event::symbol_information`; `symbol_output` writes the
response. `app` starts the request through `nom::relay` and runs the context until callbacks drain.
Relay delivery is synchronous on that thread. Event source/payload views are borrowed only for
the callback; no payload copy or queue is introduced by routing.

The local TLS integration suite checks requests, raw output, responses above 16 MiB, fragmented
responses with interleaved ping frames, size boundaries, partial messages, API error detection,
and arguments. It also verifies large WebSocket sends and receives and the library's unchanged
16 MiB default. No external service is needed for these tests.
