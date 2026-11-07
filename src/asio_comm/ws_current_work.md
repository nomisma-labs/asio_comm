**WebSocket work status — updated 2026-11-05**

The broader execution, timer-wrapper, alternate-backend, and simulation design is proposed in
[current_work.md](current_work.md). This document tracks WebSocket-specific cleanup and fixes.
The current [HTTPS and Timer scope is complete](current_work.md#https-timer-completion);
its deferred follow-ups do not reopen the completed WebSocket timer migration.

This report reviews the [interface](web_socket.cppm), [parameters](ws_params.cppm),
[implementation](web_socket.cpp), and their TODOs,
the [component contract](../../docs/asio_comm/web_socket.md), and the existing unit and integration
test sources. It also revisits the issue review in the conversation; no separate issue-report
file was found in the repository. Findings below come from source inspection and dependency
documentation. Completed stages below include their build, test, and fault-reproduction evidence.

WS-01, WS-02, WS-04, and WS-05 are complete. WS-03 is closed as unconfirmed.
Active/passive Pings are **DEV-DONE/LIVE-TEST-PENDING**. `WS-IDLE` is **RETIRED** from
WebSocket: application heartbeats are ordinary data, so subscribers own useful-data freshness.

**Current implementation/example phase is closed.** The
[2026-11-05 live connectivity report](../../apps/conn_test_summary.md) records 37 PASS,
1 NO_DATA (Crypto.com's one-second sample) and 1 SKIP (Binance SBE without credentials)
across 20 feeders and 19 providers. This is short public-endpoint/payload validation; it does
not close the longer Ping, connection-loss or closing-timeout checks in WS-06. Those checks,
WS-08 and WS-09 remain explicit follow-ups.

The final fresh `complete` build passed all **20 tests** on 2026-11-05; see the
[phase validation record](current_work.md#phase-validation). This local result leaves the
live/network-fault validation boundaries above unchanged.

| Stage ID | Batch | Complexity | Status |
| --- | --- | --- | --- |
| [WS-01](#ws-01) | Mechanical source cleanup and public-export audit | Small | COMPLETE |
| [WS-02](#ws-02) | State names, lifetime, and terminal cleanup | Medium–high | COMPLETE |
| [WS-03](#ws-03) | Send re-entry investigation | Medium | CLOSED — UNCONFIRMED; reopen only with a reproduction |
| [WS-04](#ws-04) | Parameter validation and update rules | Medium | COMPLETE |
| [WS-05](#ws-05) | Large messages and payload limits | Medium | COMPLETE |
| [WS-06](#ws-06) | Heartbeats, deadlines, and connection-loss detection | Medium–high | Pings and connection deadline DEV-DONE/LIVE-TEST-PENDING; close-timeout coverage remains |
| [WS-08](#ws-08) | Exception guarantees | Medium–high | OPEN — connected-phase audit and failure tests |
| [WS-09](#ws-09) | Optional ownership and dependency changes | Variable | OPEN — transport decisions; shared wrappers are separate work |

**Status meanings:** `COMPLETE` closes the agreed work. `DEV-DONE/LIVE-TEST-PENDING` means
implemented behavior is ready for live exchange or controlled network-fault validation; it
is not a claim that those scenarios have passed. `PARTIAL` still has development or local
fault-test work. `POSTPONED` moves work outside the current WebSocket pass.

**Deferred development:** remaining operation-timeout coverage (WS-06), exception guarantees
(WS-08), and selected TLS/transport options (WS-09).
Application-level heartbeats and market-data classification belong to exchange adapters.
`WS-TIMER-GENERATION` is **COMPLETE**: active/passive Ping waits now use `nom::timer`, as do HTTPS deadlines.

Stage IDs remain stable and identify work batches rather than completion order.
Source comments use `TODO[WS-…] (stage WS-…)`; each TODO ID identifies one concern and stays
unchanged if work is reordered. Search the source files for the exact TODO ID. The lists at the start of each stage
link to its source file. Each TODO has one owning stage, including its documentation,
implementation, and validation. Related stages use cross-references rather than duplicate
work entries; the index below is a navigation aid. Listing a TODO does not mean it is
completed. Findings without a source TODO are identified explicitly.

Complexity describes implementation and validation effort, not severity. Blocking writes
remain an accepted limitation; redesign requires a concrete reproduction or a new requirement.
Batch 8 builds on the state/lifetime contract from batch 2; exception handling for newly
changed paths belongs in those changes immediately rather than waiting for the final audit.
Keep confirmed failures distinct from design choices, and update TODO status/owner references
as part of each change.

<a id="ws-01"></a>

**WS-01. Mechanical source cleanup**

**Source TODOs covered:** none remaining; the export audit is complete.

**Complete — 2026-10-31.** Removed duplicate aliases and redundant comments, standardized
names/types, and normalized TODO formatting while retaining useful lifetime/state explanations.

Applications and test harnesses own their direct Boost includes and target dependencies;
WebSocket no longer re-exports Asio utilities. The constructor still exposes a Boost executor;
neutral wrappers are proposed in [current_work.md](current_work.md).

Separating the public module interface from the Asio/Beast/TLS backend fixed GCC 16 duplicate
symbols with direct Boost consumers. Executor/TLS ownership is hidden in `io_state`;
backend definitions, constructors, and destruction are out of line.

**Recorded validation:** GCC 16 and Clang 22 builds and unit/integration checks passed,
including direct Boost consumers, local TLS, feeders, and source/installed-package consumers.

<a id="ws-02"></a>

**WS-02. State names, lifetime, and terminal cleanup**

**Source TODOs covered:**

- [`WS-EAGER-SESSION`](web_socket.cpp) — Always create a session so web_socket::send (and others) need not check for a valid session_ pointer.
- [`WS-TLS-OWNERSHIP`](web_socket.cpp) — Review why the TLS context is at the web_socket level (could it move to session?).
- [`WS-MOVE-COPY`](web_socket.cpp) — Consider enabling move/copy construction and assignment for web_socket (why are they disabled?).
- [`WS-SETUP-STORAGE`](web_socket.cpp) — Dispose of state required only for connection setup after the connection is established (e.g. resolver).
- [`WS-INIT`](web_socket.cppm) — Introduce init (a session should start in init, not in disconnected).
- [`WS-FAILURE-NAME`](web_socket.cppm) — Rename failed to conn_failed.
- [`WS-TERMINAL-FAILURE`](web_socket.cppm) — Make conn_failed terminal, on the same level as disconnected.
- [`WS-DETACH`](web_socket.cppm) — Any terminal state (disconnected, conn_failed) should also be detached from the parent. **Retired:** retain the parent link in every state; status suppresses late completions.

**Complete — 2026-11-01.** All listed TODOs are implemented or resolved; `WS-DETACH` is retired.

| Issue | Resolution |
| --- | --- |
| Initial and terminal states | An eager session starts in `init`; reconnect replaces it. Attempts that never connect terminate in `conn_failed`; established connections terminate in `disconnected`. Repeated close preserves status/identity and emits no event. |
| Connection-failure cleanup | Connection errors and setup exceptions close the transport and cancel waits before reporting `network_error` or rethrowing an initiation exception. Applications need not close the failed connection themselves. |
| State-machine ownership | The wrapper holds and forwards to the session. Session operations use an entry-state switch and recheck status after callbacks. Pending handlers retain shared ownership; terminal guards suppress obsolete completions while parent addresses are retained. |
| Move/copy semantics | Custom moves update parent links and preserve an always-present session. Move construction prepares a fresh `init` source and can allocate/throw; assignment closes the former destination session. Copies remain disabled. |
| Delegate lifetime | Each session owns its delegate. Wrapper accessors forward to it; reconnect copies borrowed callbacks before replacing the old session. Moves carry callback storage with the session. Delegate replacement and moves stay outside active delegate invocation. |
| TLS ownership | The wrapper's `io_state` retains one configured TLS context across reconnects and keeps its heap address stable during moves. |
| Setup-only storage | `connect()` allocates a uniquely owned resolver/deadline/Host-header block inside its `try`. Success or failure releases it before notification, canceling resolution and the timer. Unused `init` sessions allocate no setup block. |

**Recorded validation:** the fresh GCC `complete` build and all seven tests passed; Clang ASan
unit, TLS loopback, and feeder suites passed. Coverage includes terminal-state behavior,
queued completions, moves, callback lifetime/reconnect, setup failures, and allocation guards.
Bootstrap's Git setup was blocked by read-only metadata; validation used CMake presets directly.

**Remaining:** [WS-06](#ws-06) for live validation and remaining operation-timeout coverage.
WS-03 is closed as unconfirmed; WS-04 and WS-05 are complete below.
Broader exception guarantees remain in [WS-08](#ws-08). Detailed behavior is in the
[component contract](../../docs/asio_comm/web_socket.md).

<a id="ws-03"></a>

**WS-03. Send re-entry investigation**

**Source TODOs covered:**

- [`WS-SEND-REENTRY`](web_socket.cpp) — Try sending from inside the ws_data receiver handler to reproduce re-entry into send.

**Closed — unconfirmed, 2026-11-01.** No WebSocket send failure was reproduced. The code retains
`UNCONFIRMED TODO[WS-SEND-REENTRY]`; this closes the investigation without claiming that every
possible operation overlap is safe. HTTP-side observations do not establish a WebSocket defect.

**Evidence:** the standalone
[Binance Spot resubscription test](../../apps/binance/binance_spot_resubscribe_test/README.md) adds
ETHUSDT from the Nth BTCUSDT market-data callback on one connection. The local TLS peer verifies
exact timing at N=1/3/100, exclusion of acknowledgements from the count, ID 2 acknowledgement,
and continued data for both symbols. Fragmented input, interleaved pings, and a ping buffered
behind the triggering message pass. A 12-second public Binance check also received the second
acknowledgement and data for both symbols; the external harness stopped the continuous tool.

**Recorded validation:** fresh GCC build and all nine tests passed; Clang ASan ordinary
loopback and dedicated resubscription suites passed. Formatting, static analysis, header,
Python, and local-link checks passed. The feeder retains its continuous-subscription role;
the dedicated tool and test remain available for future reproductions.

**Outcome:** retain the current blocking `send()` and its completed-write boolean contract.
No asynchronous send buffer, send gate, or new public send API was introduced. Blocking writes
can still stall the executor, and mixed write/control-operation cases were not exhaustively
tested; these remain documented usage limitations rather than a confirmed re-entry bug.

**Reopen condition:** a reproducible WebSocket assertion, error, hang, or delivery/order failure
with the operation sequence identified. If reopened, use the agreed sequence: reproduce,
introduce an owned deferred buffer only where required, add a session-level immediate-send
gate, then integrate lifecycle handling and tests. Buffer bounds, payload lifetime, and any
change from completed-write to acceptance semantics must be defined explicitly at that time.
These proposals are deferred and do not block the next stage.

<a id="ws-04"></a>

**WS-04. Parameter validation and update rules**

**Source TODOs covered:**

- [`WS-PARAMS`](ws_params.cppm) — Review parameter contracts and validation.

**Complete — 2026-11-01.** Mutable `params()` is replaced by `update_params(const ws_params&)`
and a read-only getter. `ws_params::validate() const` returns `std::expected<void, std::string>`
with the first invalid field's diagnostic; clients can validate independently before updating.

**Update policy 1:** the wrapper validates first, then accepts whole-object replacement only
in `init`, `conn_failed`, or `disconnected`. Invalid values throw `std::invalid_argument`;
valid updates during an active session throw `std::logic_error`. Copying before a nonthrowing
swap preserves installed settings and session behavior on failure. The session has no
parameter-update method. Settings remain fixed throughout an attempt and persist on reconnect.

Validation requires positive configured durations and positive socket buffers representable
as `int`. WS-06 provides independent optional active/passive Ping policies; absent policies
are disabled. HTTP names/values and `agent_name` are checked for syntax and supported lengths;
case-insensitive duplicates and managed handshake/framing overrides are rejected. Explicit
`User-Agent` overrides remain supported. See the [component contract](../../docs/asio_comm/web_socket.md)
for exact rules and reserved fields.

Applications, package consumers, and fixtures configure each attempt before connecting.
Tests cover boundaries, invalid HTTP fields, reserved/duplicate names, all update states,
strong failure guarantees, callback/move behavior, and on-wire settings across reconnects.
The oversized-header fixture is now covered by synchronous parameter-validation tests;
rejected upgrades and connection-initiation exceptions retain their separate coverage.

**Recorded validation:** fresh GCC `complete` build and all seven tests passed; Clang ASan
unit, TLS loopback, and feeder suites passed. Formatting, headers, Python, and reference checks
pass. Bootstrap's Git setup remains blocked by read-only metadata; direct CMake presets were used.

<a id="ws-05"></a>

**WS-05. Large messages and payload limits**

**Source TODOs covered:**

- [`WS-LARGE-MESSAGES`](ws_params.cppm) — Test sending and receiving really big messages (e.g. a huge Coinbase initial snapshot); distinguish socket buffers from message limits.

**Complete — 2026-11-01.** Large-message handling, configurable limits, and disconnect diagnostics
are implemented and validated. `WS-LARGE-MESSAGES` is covered by this completed stage.

Relevant code: `buffer_`, `on_receive()`, `send()`, and the large-message TODO.

**Reproduction — 2026-11-01:** the public Spot `exchangeInfo` response with `permissions:["SPOT"]`
contained 3,702 symbols and 17,743,355 bytes in one frame, measured with an independent TLS
WebSocket probe. Suppressing permission sets reduced a response to about 6.71 MB, which the
existing client received. These are point-in-time observations, not fixed dataset sizes.

The actual ceiling is Beast's default **16 MiB incoming-message limit**, independent of the
64 KiB socket receive buffer. A local TLS peer confirmed that the unchanged client sends close
code **1009** when a frame advertises more than 16 MiB. The reproduced public disconnect reason was
`application data after close notify (SSL routines)`: TLS shutdown masks the size diagnostic.
See [Beast's size contract](https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/ref/boost__beast__websocket__stream/read_message_max/overload1.html).

**Implemented:** `ws_params::max_message_size` is a positive unsigned incoming-message byte limit,
defaulting to 16 MiB and applied before upgrade on every attempt. Zero is rejected; the
socket-buffer settings remain independent. The
[Spot symbol provider](../../apps/binance/binance_spot_symbol_provider/README.md) requests Spot
metadata, selects a bounded 64 MiB limit, checks for API errors with basic string searches,
and relays its original response before closing. It sets `showPermissionSets:false` to suppress
unneeded permission-set contents while retaining trading filters and other symbol metadata.

The [local TLS suite](../../tests/integration/web_socket/symbol_provider_test.py) checks
byte-exact roundtrips at 65,535/65,536/65,537 bytes and above 2 MiB, a snapshot above 17 MiB,
fragmented responses with interleaved pings, and messages just below/at/above a small configured
limit. It reassembles outgoing continuation frames rather than assuming a single frame per
send. Rejected and partial input must not become complete data events; the default-limit case
also checks one terminal notification. Provider tests cover missing/non-200 status, error fields,
whitespace, and opaque response forwarding. No JSON parsing or schema validation is performed.

**Error reporting:** the TLS stream adapter retains teardown errors separately so Beast can
return its original WebSocket result. `on_disconnected.reason` identifies an oversized message
as `Incoming WebSocket message exceeds max_message_size=N bytes` and appends any TLS shutdown
error. Tests cover complete/fragmented overflow, clean teardown, a close reply during teardown,
and abrupt transport loss. An unrelated protocol-error case verifies that TLS errors do not
cause false size diagnoses. A transport failure while writing the close frame can still take
precedence before teardown starts; this remains a documented transport-error limitation.

**Recorded validation:** fresh GCC `complete` build and all eight tests passed; Clang ASan unit,
TLS loopback, symbol-provider, and exchange-feeder suites passed with protocol/TLS error preservation.
The request option change passed
the local TLS provider suite; live verification returned 6,710,072 response bytes with 3,702
empty permission sets and none populated. Formatting/static analysis, headers, naming, and
Python checks passed. Bootstrap's
Git setup was blocked by read-only metadata; direct CMake presets were used.

The receive buffer retains capacity; no shrinking policy was added without allocation
measurements. Blocking writes remain a documented limitation. Data freshness is a subscriber
responsibility; WebSocket delivers complete messages without classifying their application meaning.

<a id="ws-06"></a>

**WS-06. Heartbeats, deadlines, and connection-loss detection**

**Source TODOs covered:**

- **DEV-DONE/LIVE-TEST-PENDING:** [`WS-PING`](ws_params.cppm) — Enforce passive Ping and active Pong deadlines.
- **RETIRED — subscriber responsibility:** `WS-IDLE` — Review the receive watchdog timer and test it properly (perhaps with a local WebSocket server).
- **DEV-DONE/LIVE-TEST-PENDING:** [`WS-PEER-CLOSE`](web_socket.cppm) — Reproduce normal server disconnection (e.g. Binance resets the MD connection once per day).
- **DEV-DONE/LIVE-TEST-PENDING:** [`WS-CABLE-LOSS`](web_socket.cppm) — Reproduce hard disconnection (unplug the network cable).
- **DEV-DONE/LIVE-TEST-PENDING:** [`WS-INTERFACE-LOSS`](web_socket.cppm) — Reproduce Docker-based interface disconnection (silent loss requires a control-Ping policy or subscriber data-freshness timeout).
- **PARTIAL:** [`WS-OP-TIMEOUT`](ws_params.cppm) — Review async operation timeouts; reproduce a case when a timeout is triggered.
- **DEV-DONE/LIVE-TEST-PENDING:** [`WS-CONNECT-DEADLINE`](web_socket.cpp) — Make the connection procedure time-bound (we can use default_async_operation_timeout_sec for it).
- **COMPLETE:** [`WS-TIMER-GENERATION`](web_socket.cpp) — Replace active/passive Ping stale callback checks with generation-based timers.

For cable/interface loss, development status refers to the existing receive, timeout, and
terminal-cleanup paths. The physical/Docker fault reproductions have **not** been completed;
a failure in those checks may identify further implementation work.

**Implemented and locally checked**

| Area | Agreed behavior |
| --- | --- |
| Automatic control replies | Beast replies to server Pings independently of optional monitoring. Its idle timeout and automatic keepalive Pings remain disabled. |
| Passive Ping | Optional deadline from handshake completion and each server Ping. Only Pings refresh it; expiry aborts the transport and emits one diagnostic `on_disconnected`. |
| Active Ping | One probe at a time. `pingExpireT` uses `async_operation_timeout_sec` to bound the write; successful completion starts `pong_timeout_sec`. Every control Pong is dispatched by state; a payload mismatch warns. Failures disconnect with a diagnostic. |
| Connection deadline | One `async_operation_timeout_sec` budget covers DNS, TCP, TLS and WebSocket upgrade. Expiry aborts, selects `conn_failed` and emits one `network_error`. Setup owns the timer and releases it on success, failure or cancellation. |
| Lifecycle | Enabled timers are canceled on close/error paths. `nom::timer` suppresses canceled/superseded completions; session guards prevent late work after close, move, or reconnect. |

**WS-IDLE is retired from WebSocket (2026-11-04).** The parameter, timer, receive-activity
tracking and associated tests have been removed. Application heartbeats and acknowledgements
are normal data frames, so counting all messages cannot validate useful-data freshness.
Subscribers should classify updates, refresh their own `nom::timer`, and decide how to report
silence and reconnect. The example feeders currently have no replacement freshness timer.

**Recorded local evidence:** WebSocket unit and local TLS loopback checks passed during development.
The control-Ping suites cover silence, send blockage/recovery, payload/event ordering, cleanup,
move and reconnect. Ping checks also passed under ASan/UBSan; active checks passed in Release.
Unexpected timer errors follow `nom::timer`'s fatal policy, verified separately by fault injection.
The early-Pong failure path is not directly injected.
Watchdog-only scenarios were removed with the feature. These results do not substitute for
live or network-blackhole validation.

**DEV-DONE/LIVE-TEST-PENDING checks**

- Exercise passive/active control policies against selected live exchanges with appropriate
  parameters: cadence, replies, continued traffic, and longer sessions.
- Observe exchange-initiated Close/session rotation and verify cleanup and replacement.
- Reproduce hard cable loss, interface down/disconnection, and silent packet loss with the
  appropriate control-Ping policy or a subscriber-owned freshness timer enabled. EOF/reset tests alone do not establish blackhole detection.

**DEV-DONE/LIVE-TEST-PENDING — connection deadline (2026-11-05)**

`WS-CONNECT-DEADLINE` uses a `nom::timer` inside `connection_setup`, armed before DNS and
never restarted between stages. `on_connection_deadline()` dispatches by session state,
closes the transport and emits `network_error`. Success, failure and cancellation release
setup storage and cancel the wait. Beast's separate TCP/opening-handshake deadlines are disabled;
its client close timeout is restored after upgrade. The duplicate TCP-expiry setup calls are removed.

The local fixture covers stalled DNS, TCP, TLS and upgrade, one budget across delayed stages,
success past the former deadline, rejected upgrade, close/destruction, late cancellation,
moves and reconnect from the timeout callback. DNS interposition and the filled TCP accept
backlog cases run on Linux; all peers are local. Live connection-fault checks remain pending.
The 15 deadline cases, WebSocket unit/loopback suites and active/passive Ping suites pass in
Debug and ASan/UBSan builds.

`WS-OP-TIMEOUT` remains partial: connection and active-Ping send expiry have local reproductions;
complete the timeout checks for nonresponsive closing peers. Test-harness deadlines still
bound test execution; example applications need no connection-deadline workaround.

**COMPLETE — WebSocket adoption of the shared timer (2026-11-04)**

Active/passive Ping waits use [`nom::timer`](../../docs/asio_comm/timer.md). Canceled or
superseded completions never enter the session; the `now() < expiry()` guards and timer-error
branches are removed. Timer callbacks retain only weak session references. Protocol deadlines
and Ping state transitions are unchanged; unexpected timer errors use the shared fatal policy.
Connection waits also use `nom::timer`; Beast retains the closing-handshake timeout.

Regression coverage includes a Pong rearm followed by dispatch delayed beyond the replacement
interval, plus destruction with active/passive waits outstanding. The timer unit suite separately
checks suppression of an already queued successful expiry after a replacement deadline.

Control-Ping design, exchange policies and the active-Ping diagram/tables are documented in
[WebSocket Ping and heartbeats](../../docs/asio_comm/web_socket_ping.md);
the public behavior is documented in the [component contract](../../docs/asio_comm/web_socket.md).

<a id="ws-08"></a>

**WS-08. Exception guarantees**

**Source TODOs covered:**

- [`WS-EXCEPTIONS`](web_socket.cppm) — Consider making the interface noexcept for everything except the constructor.

**OPEN:** complete the connected-phase audit and fault tests under the agreed policy below.
The corresponding HTTPS audit is deferred beyond its
[completed current scope](current_work.md#https-timer-completion).

Relevant code: connection-stage catch blocks, connected-phase operation initiation,
`update_delegate()`, `connect()`, `send()`, and the broad `noexcept` TODO.

Connection initiation already has a defined split: synchronous initiation failure propagates,
while guarded setup failures in completion handlers report `network_error`. After entering
`connected`, read/ping/timer/control-callback setup and graceful-close initiation can throw
with work pending and state still connected/disconnecting. Error construction, delegate copying,
and delegate invocation can also fail. A blanket `noexcept` annotation would not solve this.

**Agreed policy:** keep `close()` and destruction nonthrowing. Expected operation failures are
handled within their transitions and reported through events/results. Exceptions must not escape
delegates; an exception escaping a transition reaches the outermost application handler to log
and exit. Do not resume event processing or promise cleanup/recoverable state after that failure.
Separate delegate invocation from internal setup catches so a delegate exception is not
misreported as another network failure. Synchronous validation/rejection exceptions may be
handled by the immediate caller under the operation's contract.

**Completion check:** extend the existing allocation-failure tests to connected read/timer/ping
and close initiation, and async sends if introduced. Verify terminal state, cleanup and event
cardinality for handled operation failures, and fatal propagation for escaping exceptions. Existing
`close() noexcept` allocation-guard and queued-completion coverage must continue to pass.

<a id="ws-09"></a>

**WS-09. Optional ownership and dependency changes**

**Source TODOs covered:**

- [`WS-TRANSPORT-OPTIONS`](web_socket.cpp) — Test various connection options (different TLS levels, no SSL?).

**OPEN / optional:** select the supported transport policy and validate it.

These decisions can close TODOs with a documented rationale; implementation is not required
merely because an alternative exists.

| TODO / question | Options and recommendation |
| --- | --- |
| Hide Boost behind Nomisma wrappers | Alternate communication backends and simulation now provide a concrete use case. Follow the engine/executor/timer and event-source proposal in [current_work.md](current_work.md). Direct Boost dependencies currently belong to application/test consumers; migrate those consumers to neutral contracts when the wrappers exist. Preserve module and installed-package coverage. |
| Different TLS levels / no TLS | Test the intended supported TLS versions against the local server, retaining certificate and hostname verification. Treat plain `ws://` as a separate transport feature requiring a real consumer; it is not achieved by simply accepting another URL scheme. |

Received text/binary type and structured peer close code/reason are additional API decisions
not covered by the current TODOs. Preserve existing close diagnostics; introduce
public fields only when callers need them.

**Coverage and execution notes**

The current test sources cover construction/URL integration, replacement while resolving,
repeated close/destruction, allocation-failure cases, TLS echo/push, fragmentation with
ping/pong, peer close, partial-frame EOF, certificate/hostname rejection, validated headers and agent
settings, repeated outbound pings, and a write-error transition to disconnecting. They also
include source-tree/installed-library consumers. This is an inventory of tests, not a claim
that they were executed or passed during this review.

Keep implementation changes and their corresponding docs/tests in the same batch. Use the
repository's `./bootstrap.sh <preset>` workflow for fresh validation: development checks for
small source cleanup; `complete` for behavior changes and integration consumers; sanitizer
coverage for lifetime/reentrancy changes. If local socket restrictions prevent integration
tests, report that separately from code/test failures. This report itself requires only
Markdown/link and diff review.

| Source TODO ID | Complete source TODO | Owner stage |
| --- | --- | --- |
| [`WS-PARAMS`](ws_params.cppm) | Review parameter contracts and validation. | [WS-04](#ws-04) |
| [`WS-PING`](ws_params.cppm) | Enforce passive Ping and active Pong deadlines. | [WS-06](#ws-06); DEV-DONE/LIVE-TEST-PENDING. |
| [`WS-TIMER-GENERATION`](web_socket.cpp) | Replace active/passive Ping stale callback checks with generation-based timers. | [WS-06](#ws-06); COMPLETE. |
| `WS-IDLE` | Review the receive watchdog timer and test it properly (perhaps with a local WebSocket server). | RETIRED from [WS-06](#ws-06); subscriber-owned freshness policy. |
| [`WS-OP-TIMEOUT`](ws_params.cppm) | Review async operation timeouts; reproduce a case when a timeout is triggered. | [WS-06](#ws-06); PARTIAL. |
| [`WS-LARGE-MESSAGES`](ws_params.cppm) | Test sending and receiving really big messages (e.g. a huge Coinbase initial snapshot); distinguish socket buffers from message limits. | [WS-05](#ws-05) |
| [`WS-INIT`](web_socket.cppm) | Introduce init (a session should start in init, not in disconnected). | [WS-02](#ws-02) |
| [`WS-FAILURE-NAME`](web_socket.cppm) | Rename failed to conn_failed. | [WS-02](#ws-02) |
| [`WS-TERMINAL-FAILURE`](web_socket.cppm) | Make conn_failed terminal, on the same level as disconnected. | [WS-02](#ws-02) |
| [`WS-DETACH`](web_socket.cppm) | Any terminal state (disconnected, conn_failed) should also be detached from the parent. | [WS-02](#ws-02); retired — retain the parent link and use terminal status. |
| [`WS-CABLE-LOSS`](web_socket.cppm) | Reproduce hard disconnection (unplug the network cable). | [WS-06](#ws-06); DEV-DONE/LIVE-TEST-PENDING; fault reproduction outstanding. |
| [`WS-INTERFACE-LOSS`](web_socket.cppm) | Reproduce Docker-based interface disconnection (silent loss requires a control-Ping policy or subscriber data-freshness timeout). | [WS-06](#ws-06); DEV-DONE/LIVE-TEST-PENDING; fault reproduction outstanding. |
| [`WS-PEER-CLOSE`](web_socket.cppm) | Reproduce normal server disconnection (e.g. Binance resets the MD connection once per day). | [WS-06](#ws-06); DEV-DONE/LIVE-TEST-PENDING. |
| [`WS-SEND-REENTRY`](web_socket.cpp) | Try sending from inside the ws_data receiver handler to reproduce re-entry into send. | [WS-03](#ws-03); closed — unconfirmed. |
| [`WS-EXCEPTIONS`](web_socket.cppm) | Consider making the interface noexcept for everything except the constructor. | [WS-08](#ws-08); OPEN. |
| [`WS-CONNECT-DEADLINE`](web_socket.cpp) | Make the connection procedure time-bound (we can use default_async_operation_timeout_sec for it). | [WS-06](#ws-06); DEV-DONE/LIVE-TEST-PENDING. |
| [`WS-SETUP-STORAGE`](web_socket.cpp) | Dispose of state required only for connection setup after the connection is established (e.g. resolver). | [WS-02](#ws-02) |
| [`WS-TRANSPORT-OPTIONS`](web_socket.cpp) | Test various connection options (different TLS levels, no SSL?). | [WS-09](#ws-09); OPEN / optional. |
| [`WS-EAGER-SESSION`](web_socket.cpp) | Always create a session so web_socket::send (and others) need not check for a valid session_ pointer. | [WS-02](#ws-02) |
| [`WS-MOVE-COPY`](web_socket.cpp) | Consider enabling move/copy construction and assignment for web_socket (why are they disabled?). | [WS-02](#ws-02) |
| [`WS-TLS-OWNERSHIP`](web_socket.cpp) | Review why the TLS context is at the web_socket level (could it move to session?). | [WS-02](#ws-02) |

**Future work:** choose remaining close-timeout coverage (WS-06), the exception audit (WS-08),
or transport-option decisions (WS-09). Run live Ping/connection-loss validation alongside
those tasks. Application-level heartbeats remain exchange-adapter work. Keep the completed control-Ping scope closed
unless validation exposes a concrete defect or a new requirement is agreed.
