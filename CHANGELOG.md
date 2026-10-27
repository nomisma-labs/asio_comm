# Changelog

Notable changes to this repo. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and versions follow [Semantic Versioning](https://semver.org); how to release:
[docs/kit/build.md](docs/kit/build.md#versioning).

## [Unreleased]

### Changed

- Sync cmake_template v0.11.2 and its documentation correction at `8ad7a4e`, adopting
  `import std;` with CMake 4.4.x and per-target standard-library module support.
- Pin base v0.2.1, json_config v0.2.1, assembly v0.3.1, and spd_logger v0.4.1.
  The configuration and logging repositories also pin base v0.2.1.

### Added

- Add pinned `base`, `json_config`, `spd_logger`, and `assembly` repository dependencies.
- Add `asio_comm_example`, which reads and validates JSON configuration and routes startup,
  example logging, and completion through a two-node assembly/relay workflow, with command-line
  overrides and a config-only validation mode.
- Initialize the repository from cmake_template v0.11.1 for WebSocket, HTTP and other
  communication protocol implementations, initially for cryptocurrency exchanges.
