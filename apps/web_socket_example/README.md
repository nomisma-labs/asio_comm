# WebSocket example

[web_socket_example](main.cpp) connects to a TLS WebSocket endpoint, optionally sends one
message, prints the first reply, and closes the connection. The [component documentation](../../docs/web_socket.md)
describes its state machine, events and usage constraints.

## Build and run

Install the shared [build prerequisites](../../docs/kit/build.md#toolchain), OpenSSL development
files and a system CA certificate bundle. Run these commands from the repository root:

```bash
cmake --workflow --preset dev
./build/dev/bin/web_socket_example
./build/dev/bin/web_socket_example 'wss://YOUR_HOST:443/YOUR_PATH' 'hello'
```

With no arguments, the example connects to Postman's echo endpoint at
`wss://ws.postman-echo.com/raw` and sends `hello`. Supply a URL to use another endpoint.
The example connects, optionally sends one text message, prints the first received message,
and calls `close()`. Supply a URL without a message to wait for an unsolicited server message.
It exits with status 1 on failure. TLS verifies both the
certificate chain and hostname; a private CA can be supplied
using OpenSSL's `SSL_CERT_FILE` environment variable.

Defaults and argument parsing live in [config.h](config.h). `app_config::validate() const` returns
`std::expected<void, std::string>`; `main()` reports invalid settings before constructing the client.

## Using the client

The client has a [module interface](../../core/asio_comm/web_socket.cppm) and a private
[implementation unit](../../core/asio_comm/web_socket.cpp).
Link `nom::asio_comm` and `import nom.asio_comm;`. It uses base's
`NOM_ENUM` and `nom::url`, Assembly's `nom::delegate`, and the shared logger.

The app owns its Boost.Asio context. Include `boost/asio/any_io_executor.hpp` and
`boost/asio/io_context.hpp` before module imports, link `Boost::headers` directly, and use
`boost::asio::io_context` in application code.
The WebSocket module does not re-export these utilities.

Source consumers must enable standard-library module detection before their first `project()`
and use `IMPORT_STD` in their kit target helpers (or `nom_enable_std_modules(target)` for other targets);
see [module setup](../../docs/kit/build.md#modules).

The client owns its endpoint as a `nom::url`, exposed by `client.url()`; its `port()` is numeric.
Construct from a `wss` URL, or supply a host, `std::uint16_t` port, and encoded path/query:
`nom::web_socket client(io.get_executor(), "feed.example", 443, "/stream");`.
The component constructor always uses the `wss` scheme and validates the components through `nom::url`.

### Parameters and headers

Configure the client through `update_params()`. `params()` returns a read-only view of its
`nom::ws_params`. Headers are
`nom::ws_params::header` entries with owning `name` and `value` strings:

```cpp
nom::web_socket client(io.get_executor(), nom::url("wss://feed.example/stream"));
auto params = client.params();
params.headers = {{.name = "Authorization", .value = "Bearer " + token}};
params.agent_name = "my-feed-client/1.0";
params.active_ping = nom::ws_params::client_ping_probe{};
client.update_params(params);
client.connect(callbacks);
```

`callbacks` is a `nom::web_socket::delegate` receiving `nom::event::on_connected`,
`nom::event::ws_data`, `nom::event::on_disconnected`, and `nom::event::network_error`.
`ws_params::validate()` returns `std::expected<void, std::string>` with the first invalid field's
diagnostic. `update_params()` performs the same validation and throws `std::invalid_argument`
for invalid values. Valid updates are allowed only in `init`, `conn_failed`, or `disconnected`;
active-session updates throw `std::logic_error`. Rejected updates preserve the installed settings.
Serialize updates with client calls and completion handlers. Settings persist across reconnects;
`update_params({})` restores defaults while inactive.

| Parameter | Default | Used for |
| --- | --- | --- |
| `headers` | Empty | Custom HTTP upgrade headers; applied after `agent_name`, so a User-Agent header overrides it |
| `agent_name` | `nomisma/asio_comm` | HTTP upgrade User-Agent |
| `passive_ping` | `std::nullopt` | Expect a server control Ping within `ping_timeout_sec` (default 20) after handshake and after each Ping. A missed deadline closes the transport and reports a timeout through `on_disconnected`. Data and Pongs do not refresh it. |
| `active_ping` | `std::nullopt` | Starts periodic Pings and counts received Pongs as activity; `ping_period_sec` defaults to 20. `pong_timeout_sec` also defaults to 20, but enforcement is pending. |
| `async_operation_timeout_sec` | `30` | The TCP-connect deadline, which also covers the subsequent TLS handshake |
| `receive_watchdog` | `std::nullopt` | Optional `receive_data_watchdog`; `timeout_sec` defaults to 20 when enabled. Current receive-activity check samples every N + N/2 seconds and closes when whole elapsed seconds exceed N + N/5. |
| `socket_send_buf_size`, `socket_recv_buf_size` | `65536` bytes each | Socket options applied after TCP connect |

Accepted settings apply to the next connection attempt and remain fixed while it is active.
Configured durations must be positive; `std::nullopt` disables an optional policy. Buffer sizes
must fit a positive `int`. The active Pong timeout is validated but not yet enforced. HTTP
fields must have valid syntax and supported lengths, with no duplicate names or overrides of
managed handshake fields. An explicit `User-Agent` header may override `agent_name`.
WebSocket handshake/close timeouts
still use Beast's client defaults. Headers are not attached to subsequent data frames.

The `disconnecting` status marks an internal graceful close: new sends are rejected and timer
work stops. `close()` is `noexcept`: it detaches immediately and attempts transport cleanup
and cancellation independently, suppressing cleanup exceptions without a final event.

The [WebSocket documentation](../../docs/web_socket.md) covers the state machine, events,
usage constraints, deferred work and validation coverage.

## Validation

The integration suite uses a local TLS peer to test echo, fragmented messages with an
interleaved ping, server-pushed data, peer close, incomplete input, certificate rejection,
validated headers/User-Agent settings, rejected active updates, retained/reset parameters,
and repeated configured outbound pings.
No public endpoint is needed for tests.

See the [project overview](../../README.md) for repository dependencies and shared build instructions.
