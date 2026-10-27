# asio_comm

<!-- repo-description:begin -->
`asio_comm` will provide C++ implementations of WebSocket, HTTP and other communication
protocols, initially for connectivity to cryptocurrency exchanges. The repository currently
contains the shared build infrastructure and a configuration/logging workflow example; protocol
implementations will be added as development progresses.
<!-- repo-description:end -->

## Build

Install the tools listed in [docs/kit/build.md](docs/kit/build.md#toolchain), then:

```bash
git submodule update --init --recursive
./bootstrap.sh                   # fresh builds, tests and checks for every preset but release
cmake --workflow --preset dev    # configure, build and test one preset
```

Everyday commands, the presets and what bootstrap checks: [docs/kit/build.md](docs/kit/build.md).

## Dependencies

The repositories under `external/` are pinned Git submodules and are loaded through
`FetchContent`. `base` supplies the shared `nom.config_types` module, `json_config` loads
application settings, `spd_logger` provides synchronous logging, and `assembly` provides
application composition and event routing.

| Repository | Pin |
|---|---|
| `base` | `v0.2.0` |
| `json_config` | `v0.2.0` |
| `spd_logger` | `v0.4.0` |
| `assembly` | `v0.3.0` |

`base` owns the common configuration types; `json_config` supplies JSON mapping and
re-exports them. Declaring `base` first lets the configuration and logging libraries
share one instance.

## Configuration and logging example

Build and run from the repository root:

```bash
cmake --workflow --preset dev
./build/dev/bin/asio_comm_example \
    --config build/dev/config/asio_comm_example/asio_comm_example.json
```

The [example](apps/asio_comm_example/main.cpp) reads `app_name`, `message`, and
`debug_logging` from its [JSON config](apps/asio_comm_example/config/asio_comm_example.json).
It validates the settings and initializes the console logger before running a two-node
`nom::assembly` through `nom::relay`:

1. `startup_node` handles `start_app`, logs "app started with config" and the selected
   settings, then forwards the request to `log_messages_node`.
2. `log_messages_node` writes the configured message and example WARN/ERROR logs, then
   sends an `app_finished` response back to `startup_node`.
3. `startup_node` consumes the response by logging completion at ALWAYS level.

The whole workflow runs synchronously; event references remain valid until the request
returns. The warning and error are demonstrations; the app exits successfully. Setting
`debug_logging` to `false` suppresses DEBUG output. `--check-config` exits before logger
initialization or relay construction.

The build stages the sample config beside the build's `bin/` directory. Use `--config`
as above to select it explicitly; without a selected or discovered config, the app uses
its C++ defaults. The standard `json_config` environment lookup also applies.

Override a setting or validate without initializing the logger:

```bash
./build/dev/bin/asio_comm_example \
    --config build/dev/config/asio_comm_example/asio_comm_example.json \
    --set /debug_logging=false
./build/dev/bin/asio_comm_example \
    --config build/dev/config/asio_comm_example/asio_comm_example.json --check-config
```

Invalid settings, an unreadable config, or unknown arguments produce a diagnostic on
standard error and exit status 1.

`./bootstrap.sh ci` builds the example and runs integration checks for the full relay
workflow and config-only startup suppression. These integration checks are also included
in `complete`; the `dev` preset skips them.

## Docs

Agents start at [AGENTS.md](AGENTS.md), people at [docs/](docs/README.md). Adding a library, an
app, tests or a dependency: [docs/kit/howto.md](docs/kit/howto.md). Releases: [CHANGELOG.md](CHANGELOG.md).
