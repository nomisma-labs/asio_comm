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

Library sources are in `src/asio_comm/`. The library target is `nom::asio_comm`;
its C++ module is `nom.asio_comm`.

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

The sole direct Git submodule is `external/nom_core`, loaded through `FetchContent`.
It pins `base`, `assembly`, `spd_logger`, and `json_config` together. Initialize submodules
recursively to fetch the complete set. Library code, examples and tests use the `nom.core`
module and `nom::core` target for common types, URL utilities, composition, logging and
JSON configuration. Include macro headers directly from their owning libraries.

`nom::asio_comm` publicly links `nom::core`; its installed package finds `nom_core 0.3`.
Core modules and their logging configuration JSON mappings are available to consumers
through the same dependency bundle. See the [core usage guide](external/nom_core/docs/nom_core/README.md).

The communication clients also use Boost.Asio/Beast and OpenSSL. Install the OpenSSL development
package and a system CA certificate bundle; the loopback integration test also uses the
`openssl` command-line tool and Python's standard library. The HTX feeder additionally needs the
zlib development package to handle compressed application heartbeats.

| Repository | Pin |
|---|---|
| `nom_core` | `v0.3.0` |

See [nom_core's dependency list](external/nom_core/README.md#dependencies) for its component pins.

## Documentation

- [Documentation index](docs/README.md)
- [Development recipes](docs/kit/howto.md)
- [Changelog](CHANGELOG.md)
- [TODO and work plans](TODO.md)
- [Agent instructions](AGENTS.md)

## License

Project-owned code and documentation use the [Sustainable Use License 1.0](LICENSE)
and are source available. Copyright belongs to Alexey Tkachenko. The synchronized
build kit and any MIT exceptions listed in `project_license.json` retain their
[MIT notice](cmake/kit/LICENSE); dependencies retain their own licenses.
