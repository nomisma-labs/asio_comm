# asio_comm

<!-- repo-description:begin -->
`asio_comm` will provide C++ implementations of WebSocket, HTTP and other communication
protocols, initially for connectivity to cryptocurrency exchanges. The repository contains an
initial TLS WebSocket client, public exchange market-data feeders, and WebSocket and logging/relay examples.
<!-- repo-description:end -->

## Components

- [WebSocket](docs/web_socket.md): TLS client, state machine, events and usage constraints.
  The library target is `nom::asio_comm`; its C++ module is `nom.asio_comm`.

## Example applications

- [Binance Spot feeder](apps/binance_spot_feeder/README.md): subscribe to best bid/ask, individual trades and depth
  streams for selected symbols and log incoming messages.
- [Binance perpetual futures feeder](apps/binance_perp_feeder/README.md): log USDⓈ-M best bid/ask,
  individual trades and depth streams on one connection.
- [Bitflyer Spot feeder](apps/bitflyer_spot_feeder/README.md): log ticker, executions and board data.
- [Deribit perpetual futures feeder](apps/deribit_perp_feeder/README.md): log public ticker,
  trades and order-book updates.
- [OKX Spot feeder](apps/okex_spot_feeder/README.md): log ticker, trades and depth with text heartbeats.
- [WebSocket example](apps/web_socket_example/README.md): connect, send a message and print a reply;
  includes client setup and parameter examples.
- [Logging and relay example](apps/asio_comm_example/README.md): configure logging in C++
  and run an Assembly relay workflow.

## Build

Install the tools listed in the [build guide](docs/kit/build.md#toolchain), then run from the repository root:

```bash
git submodule update --init --recursive
cmake --workflow --preset dev
```

`./bootstrap.sh` performs fresh builds, tests and quality checks for every preset except `release`.
See the [build guide](docs/kit/build.md) for toolchain requirements, presets and everyday commands.

## Dependencies

The repositories under `external/` are pinned Git submodules and are loaded through
`FetchContent`. `base` supplies shared utilities and configuration types,
`spd_logger` provides synchronous logging, and `assembly` provides
application composition and event routing.

The WebSocket client also uses Boost.Asio/Beast and OpenSSL. Install the OpenSSL development
package and a system CA certificate bundle; the loopback integration test also uses the
`openssl` command-line tool and Python's standard library.

| Repository | Pin |
|---|---|
| `base` | `v0.3.0` |
| `spd_logger` | `v0.5.0` |
| `assembly` | `v0.4.0` |

## Documentation

- [Documentation index](docs/README.md)
- [Development recipes](docs/kit/howto.md)
- [Changelog](CHANGELOG.md)
- [Agent instructions](AGENTS.md)
