# Changelog

Notable changes to this repo. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and versions follow [Semantic Versioning](https://semver.org); how to release:
[docs/kit/build.md](docs/kit/build.md#versioning).

## [Unreleased]

### Added

- `nom::https`: asynchronous HTTP/1.1 over verified TLS, validated requests and parameters,
  bounded request queuing, connection/request/shutdown deadlines, move support and an example app.
- `nom::timer`: one-shot scheduling with generation-safe cancellation/rearming, move support,
  fatal diagnostics for unexpected backend errors and an example app.
- Optional active/passive WebSocket control-Ping policies with deadline enforcement, plus one
  connection deadline covering DNS, TCP, TLS and WebSocket upgrade.
- Twenty public market-data feeders and nineteen WebSocket/HTTPS symbol providers, with per-app
  API documentation; binary feeds log message type and size without decoding market payloads.
- Shared application termination through Assembly/relay: Ctrl-C/SIGTERM and optional
  `--exit-after-sec` limits for every feeder and provider.
- `tools/asio_comm/conn_test.sh`: sequential live checks, optional group builds, process deadlines,
  payload statistics and Markdown reports. The saved connectivity report records the current coverage.
- Local TLS and fault-injection coverage for lifecycle, deadlines, large messages, moves,
  cancellation, exchange protocols and application termination; offline connectivity-runner tests.

### Changed

- WebSocket session lifetime, initial/terminal states and move operations; delegates and
  connection-only storage now live with the session, with consistent failure cleanup.
- WebSocket parameters now use validation and `update_params()` instead of mutable parameter
  access. Public exports are narrowed; applications include their own Boost executor/context types.
- HTTPS, WebSocket and application timers use `nom::timer` to suppress canceled/superseded callbacks.
- Exchange examples are grouped by exchange under `apps/`, with `feeders` and `providers` build
  targets. They remain explicitly buildable in `dev` while excluded from its default build.

### Removed

- The WebSocket receive-data watchdog. Subscribers own useful-data freshness checks because
  application heartbeats and acknowledgements also arrive as data frames.

## [0.1.0] - 2026-10-29

### Added

- Initial TLS WebSocket client (`nom::web_socket`) built on Boost.Asio and Boost.Beast.
