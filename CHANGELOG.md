# Changelog

Notable changes to this repo. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and versions follow [Semantic Versioning](https://semver.org); how to release:
[docs/kit/build.md](docs/kit/build.md#versioning).

## [Unreleased]

### Added

- Add pinned `base`, `json_config`, `spd_logger`, and `assembly` repository dependencies.
- Add `asio_comm_example`, which reads and validates JSON configuration and routes startup,
  example logging, and completion through a two-node assembly/relay workflow, with command-line
  overrides and a config-only validation mode.
- Initialize the repository from cmake_template v0.11.1 for WebSocket, HTTP and other
  communication protocol implementations, initially for cryptocurrency exchanges.
