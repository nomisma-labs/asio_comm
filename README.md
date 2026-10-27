# asio_comm

<!-- repo-description:begin -->
`asio_comm` will provide C++ implementations of WebSocket, HTTP and other communication
protocols, initially for connectivity to cryptocurrency exchanges. The repository currently
contains the shared build infrastructure; protocol implementations and tests will be added
as development progresses.
<!-- repo-description:end -->

## Build

Install the tools listed in [docs/kit/build.md](docs/kit/build.md#toolchain), then:

```bash
./bootstrap.sh                   # fresh builds, tests and checks for every preset but release
cmake --workflow --preset dev    # configure, build and test one preset
```

Everyday commands, the presets and what bootstrap checks: [docs/kit/build.md](docs/kit/build.md).

## Docs

Agents start at [AGENTS.md](AGENTS.md), people at [docs/](docs/README.md). Adding a library, an
app, tests or a dependency: [docs/kit/howto.md](docs/kit/howto.md). Releases: [CHANGELOG.md](CHANGELOG.md).
