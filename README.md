# asio_comm

<!-- repo-description:begin -->
`asio_comm` provides C++ TLS WebSocket and asynchronous HTTPS/HTTP/1.1 clients, plus a
generation-safe timer, for connectivity to cryptocurrency exchanges. The repository includes
public market-data feeders, WebSocket and HTTPS symbol providers, communication/timer/logging
examples, and a live connectivity test runner.
<!-- repo-description:end -->

## Components

- [WebSocket](docs/asio_comm/web_socket.md): TLS client, state machine, events and usage constraints.
- [HTTPS](docs/asio_comm/https.md): asynchronous HTTP/1.1 requests, bounded queuing and deadlines.
- [Timer](docs/asio_comm/timer.md): one-shot scheduling with generation-safe cancellation.

The library target is `nom::asio_comm`; its C++ module is `nom.asio_comm`.

## Example applications

See the [application overview](apps/README.md) for market-data feeders, symbol providers,
exchange hosting information, and links to each example's build and usage instructions.

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

The communication clients also use Boost.Asio/Beast and OpenSSL. Install the OpenSSL development
package and a system CA certificate bundle; the loopback integration test also uses the
`openssl` command-line tool and Python's standard library. The HTX feeder additionally needs the
zlib development package to handle compressed application heartbeats.

| Repository | Pin |
|---|---|
| `base` | `v0.3.0` |
| `spd_logger` | `v0.5.0` |
| `assembly` | `v0.4.0` |

## Documentation

- [Documentation index](docs/README.md)
- [Development recipes](docs/kit/howto.md)
- [Changelog](CHANGELOG.md)
- [TODO and work plans](TODO.md)
- [Agent instructions](AGENTS.md)
