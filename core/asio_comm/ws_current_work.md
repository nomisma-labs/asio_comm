**WebSocket review and proposed work — 2026-10-31**

The broader execution, timer-wrapper, alternate-backend, and simulation design is proposed in
[current_work.md](current_work.md). This document tracks WebSocket-specific cleanup and fixes.

This report reviews the [interface](web_socket.cppm), [implementation](web_socket.cpp), and their TODOs,
the [component contract](../../docs/web_socket.md), and the existing unit and integration
test sources. It also revisits the issue review in the conversation; no separate issue-report
file was found in the repository. Findings below come from source inspection and dependency
documentation. Completed stages below include their build, test, and fault-reproduction evidence.

WS-01, WS-02, WS-04, and WS-05 are complete. WS-03 is closed as unconfirmed; WS-06 is next.
Each stage includes its contract
documentation and focused tests; building a general failure test framework is not a
prerequisite for the work.

| Stage ID | Batch | Complexity | Result |
| --- | --- | --- | --- |
| [WS-01](#ws-01) | Mechanical source cleanup and public-export audit | Small | Consistent code and justified public names |
| [WS-02](#ws-02) | State names, lifetime, and terminal cleanup | Medium–high; split into small steps | Predictable status and ownership transitions |
| [WS-03](#ws-03) | Send re-entry investigation | Medium (investigation) | Closed — unconfirmed; baseline passes |
| [WS-04](#ws-04) | Parameter validation and update rules | Medium | Settings with defined values and application times |
| [WS-05](#ws-05) | Large messages and payload limits | Medium | Complete: verified snapshot handling and bounded message size |
| [WS-06](#ws-06) | Heartbeats, deadlines, and connection-loss detection | Medium–high | Defined liveness behavior with reproducible tests |
| [WS-08](#ws-08) | Exception guarantees | Medium–high | Reliable cleanup when operation initiation fails |
| [WS-09](#ws-09) | Optional ownership and dependency changes | Variable | Changes justified by actual requirements |

Stage numbers identify work batches rather than completion order; WS-06 is next.
Other stage IDs remain unchanged.
Source comments use `TODO[WS-…] (stage WS-…)`; each TODO ID identifies one concern and stays
unchanged if work is reordered. Search either source file for the exact TODO ID. The lists at the start of each stage
link to its source file. Each TODO has one owning stage, including its documentation,
implementation, and validation. Related stages use cross-references rather than duplicate
work entries; the index below is a navigation aid. Listing a TODO does not mean it is
completed. Findings without a source TODO are identified explicitly.

Complexity describes implementation and validation effort, not severity. Blocking writes
are a significant issue even though their redesign comes after the requested cleaning work.
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
| State-machine ownership | The wrapper holds and forwards to the session. Session operations use an entry-state switch and recheck status after callbacks. Pending handlers retain shared ownership; terminal guards suppress obsolete completions while parent addresses are retained. |
| Move/copy semantics | Custom moves update parent links and preserve an always-present session. Move construction prepares a fresh `init` source and can allocate/throw; assignment closes the former destination session. Copies remain disabled. |
| Delegate lifetime | Each session owns its delegate. Wrapper accessors forward to it; reconnect copies borrowed callbacks before replacing the old session. Moves carry callback storage with the session. Delegate replacement and moves stay outside active delegate invocation. |
| TLS ownership | The wrapper's `io_state` retains one configured TLS context across reconnects and keeps its heap address stable during moves. |
| Setup-only storage | `connect()` allocates a uniquely owned resolver/Host-header block inside its `try`. Success or failure releases it before notification; resolver destruction cancels pending resolution. Unused `init` sessions allocate no setup block. |

**Recorded validation:** the fresh GCC `complete` build and all seven tests passed; Clang ASan
unit, TLS loopback, and feeder suites passed. Coverage includes terminal-state behavior,
queued completions, moves, callback lifetime/reconnect, setup failures, and allocation guards.
Bootstrap's Git setup was blocked by read-only metadata; validation used CMake presets directly.

**Next:** [WS-06](#ws-06) for heartbeats, deadlines, timer cleanup, and peer-close handling.
WS-03 is closed as unconfirmed; WS-04 and WS-05 are complete below.
Broader exception guarantees remain in [WS-08](#ws-08). Detailed behavior is in the
[component contract](../../docs/web_socket.md).

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

- [`WS-PARAMS`](web_socket.cppm) — Review parameter contracts and validation.

**Complete — 2026-11-01.** Mutable `params()` is replaced by `update_params(const ws_params&)`
and a read-only getter. `ws_params::validate() const` returns `std::expected<void, std::string>`
with the first invalid field's diagnostic; clients can validate independently before updating.

**Update policy 1:** the wrapper validates first, then accepts whole-object replacement only
in `init`, `conn_failed`, or `disconnected`. Invalid values throw `std::invalid_argument`;
valid updates during an active session throw `std::logic_error`. Copying before a nonthrowing
swap preserves installed settings and session behavior on failure. The session has no
parameter-update method. Settings remain fixed throughout an attempt and persist on reconnect.

Validation requires positive configured durations and positive socket buffers representable
as `int`. WS-06 separates optional idle monitoring from optional Ping policies; absent policies
are disabled. HTTP names/values and `agent_name` are checked for syntax and supported lengths;
case-insensitive duplicates and managed handshake/framing overrides are rejected. Explicit
`User-Agent` overrides remain supported. See the [component contract](../../docs/web_socket.md)
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

- [`WS-LARGE-MESSAGES`](web_socket.cppm) — Test sending and receiving really big messages (e.g. a huge Coinbase initial snapshot); distinguish socket buffers from message limits.

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
measurements. Blocking writes remain a documented limitation; slow fragmented-message liveness
continues in WS-06.

<a id="ws-06"></a>

**WS-06. Heartbeats, deadlines, and connection-loss detection**

**Source TODOs covered:**

- [`WS-PING`](web_socket.cppm) — Enforce passive Ping and active Pong deadlines.
- [`WS-IDLE`](web_socket.cppm) — Review the receive watchdog timer and test it properly (perhaps with a local WebSocket server).
- [`WS-OP-TIMEOUT`](web_socket.cppm) — Review async operation timeouts; reproduce a case when a timeout is triggered.
- [`WS-CONNECT-DEADLINE`](web_socket.cpp) — Make the connection procedure time-bound (we can use default_async_operation_timeout_sec for it).
- [`WS-CABLE-LOSS`](web_socket.cppm) — Reproduce hard disconnection (unplug the network cable).
- [`WS-INTERFACE-LOSS`](web_socket.cppm) — Reproduce Docker-based interface disconnection (a soft disconnection of a virtual interface may only be caught by the receive watchdog timer).
- [`WS-PEER-CLOSE`](web_socket.cppm) — Reproduce normal server disconnection (e.g. Binance resets the MD connection once per day).

Heartbeat research, exchange requirements, and interface options are in
[ping_work.md](ping_work.md). It distinguishes transport probes, application heartbeats,
private-feed leases, and market-data freshness. Parameter migration is implemented; Ping
deadline enforcement and useful-data idle semantics remain pending.

**Preparatory parameter migration — 2026-11-03:**

- `active_ping` replaces the outbound enable flag and supplies `ping_period_sec`; automatic
  Pong replies and current Ping send/completion behavior are preserved. `passive_ping` and
  `pong_timeout_sec` are validated but their deadlines are not yet enforced.
- `receive_watchdog` is an optional `receive_data_watchdog`, disabled by default. When enabled,
  `timeout_sec` must be positive and defaults to 20. Existing sampling,
  grace, whole-second rounding, and mixed receive-activity classification are preserved.
- Both session timer objects are optional: `connect()` constructs only the enabled timers,
  waits start after the handshake, and cancellation skips absent timers.
- Examples and tests use the new parameters. The local TLS fixture covers enabled/disabled
  idle monitoring, independent Ping cadence, and automatic Pong replies with idle disabled.

Validation: GCC `complete` build, 18 WebSocket unit cases, and local TLS loopback checks pass.
Symbol-provider, resubscription, exchange-feeder/app, and example-workflow checks also pass.
Formatting, source headers, Python checks, and `git diff --check` pass.

**Lifecycle follow-up from WS-02 (no separate source TODO):** make idle/ping cancellation
consistent across peer close, read errors, explicit close, and graceful-close completion.
In particular, peer-close/read-error paths can leave a ping wait pending until it expires.

**Pending lifecycle step:** reproduce terminal timer cleanup with outbound pings enabled and a
delegate that leaves the client alive after `on_disconnected`. Check peer close and read errors
without an application `close()` masking the cleanup. Both timers should stop without waiting
for the next ping interval, rearming, or emitting another terminal event. Address this lifecycle
case before changing idle-activity policy or enforcing Ping/Pong deadlines.

**WIP:** application connection deadlines have been removed from all examples, including the
WebSocket example's overall response timer that also covered connection setup. An overall
connection deadline belongs to the client timeout work here. Test-harness deadlines remain
to bound test execution, and the OKEx application heartbeat remains a protocol requirement.
The removal passed the GCC `complete` build and the unit, local TLS loopback, and exchange-feeder
test suites. No new tests were added for this simplification.

Relevant code: `on_control()`, `start_receive_watchdog_timer()`, `on_receive_watchdog_timer()`, ping handlers, and
the resolve/connect/handshake chain.

Current behavior confirmed by inspection:

- Idle monitoring is disabled by default. When enabled, a quiet but healthy peer can be disconnected even with outbound pings disabled.
- Received ping frames count as activity; pongs count only when `active_ping` is present.
- Data activity is recorded at complete-message delivery, so a slow fragmented message can
  be making network progress without refreshing `last_recv_ts_`.
- With a watchdog enabled at 20 seconds, idle checks run every 30 seconds and compare whole elapsed seconds against
  24. This is periodic sampling, not an exact 24- or 30-second inactivity deadline.
- Ping send failure logs and rearms; a failed ping timer logs and stops. Neither path directly
  establishes a terminal outcome. A successful ping send does not establish receipt of a pong.
- DNS has no library deadline. TCP connect and TLS share one configured deadline; WebSocket
  handshake/close use the suggested client timeout, 30 seconds in the installed Beast version.

**Decision — 2026-11-01: retain our own idle timer.** Inactivity detection is a core client
responsibility whose policy, transitions, and tests must remain explicit in this code.
Keep Beast's `idle_timeout` disabled (`none()`) and its automatic idle pings disabled. This
stage improves the existing timer rather than replacing it with Beast's idle mechanism.

Implement and validate the policy explicitly:

- Build on the independent optional idle timeout and Ping policies above; enforce the passive
  Ping and active Pong deadlines without changing the useful-data idle contract implicitly.
- Define exactly what refreshes activity: complete data messages, fragmented-message progress,
  incoming pings, and incoming pongs. Decide which of these prove the activity the caller needs;
  receiving control frames alone must not silently imply that market data is arriving.
- Use steady-clock durations without whole-second truncation. Schedule checks against the
  selected inactivity deadline instead of deriving periodic sampling and grace from ping cadence.
- Use explicit `switch`/`case` mappings for expiry, cancellation, timer errors, and terminal
  cleanup. Queued expiries and obsolete-session completions must not rearm or notify after close
  or reconnect; an idle failure must produce one terminal outcome with a clear reason.
- Test activity near the deadline, delayed timer delivery, control-only traffic, fragmented
  data, silence, cancellation with an expiry already queued, and reconnect before callbacks drain.

Beast's WebSocket handshake/close timeout remains separate from our idle policy. Keep TCP
deadlines disabled after control passes to the WebSocket stream.
[Beast timeout contract](https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/using_websocket/timeouts.html).

**Small source cleanup (no separate source TODO):** review the repeated `expires_never()` in
`on_handshake()`. `on_ssl_handshake()` already disables the TCP deadline; verify that no path
re-enables it before removing the duplicate call and its exception-reporting branch. This
changes a setup/error path and needs focused validation.

Document automatic replies to peer pings separately from optional outbound pings, and state
that idle monitoring is independently enabled by `receive_watchdog`. TCP keepalive,
WebSocket control frames, and exchange JSON/text heartbeats serve different purposes. Keep
exchange-specific messages in the feeder/protocol layer. Do not add pong matching if the
actual requirement is merely to observe any peer activity.

**Connection-timeout options:** one overall attempt budget covering DNS through WebSocket
upgrade, or separate stage budgets. Prefer one overall budget initially, with the failing
stage included in diagnostics; add stage controls only when required. A separate attempt
timer can cancel the active work without trying to reuse the TCP deadline for WebSocket I/O.
Define a bounded close policy too: detection time plus graceful-close time determines when
the caller observes final disconnection. If the transport is already failed, consider aborting
directly rather than trying a normal close handshake.
Document which paths abort through `close()` and which use graceful `shutdown()`, including
their notification and cancellation guarantees.

**Completion check:** local tests for idle-but-responsive peer, ignored pings, unsolicited
pongs, slow fragmented data, stalled TLS/upgrade, EOF, reset, peer close, and a peer that does
not answer close. Use bounded timing ranges and verify the event reason. Reproduce DNS and TCP
blackholes with controlled infrastructure or a narrow test seam; an unreachable address alone
does not reliably reproduce a timeout. Add optional network-namespace/Docker fault tests for
packet drops and interface loss, and a manual cable-removal recipe. A reset test does not prove
blackhole detection. Simulate periodic server close locally rather than waiting for an exchange.

<a id="ws-08"></a>

**WS-08. Exception guarantees**

**Source TODOs covered:**

- [`WS-EXCEPTIONS`](web_socket.cppm) — Consider making the interface noexcept for everything except the constructor.

Relevant code: connection-stage catch blocks, connected-phase operation initiation,
`update_delegate()`, `connect()`, `send()`, and the broad `noexcept` TODO.

Connection initiation already has a defined split: synchronous initiation failure propagates,
while guarded setup failures in completion handlers report `network_error`. After entering
`connected`, read/ping/timer/control-callback setup and graceful-close initiation can throw
with work pending and state still connected/disconnecting. Error construction, delegate copying,
and delegate invocation can also fail. A blanket `noexcept` annotation would not solve this.

**Recommended policy:** keep `close()` and destruction nonthrowing; retain a documented
nonthrowing-delegate precondition; establish cleanup-before-propagation or cleanup-before-report
for each operation boundary. Separate callback invocation from internal setup catches so a
callback exception is not misreported as a second network failure. Even if allocating an error
message fails, the session must be left safely terminal. Ordinary I/O failures should retain
their normal event/result path.

**Alternative:** a fully nonthrowing public API with explicit result/error values. This requires
decisions about allocation failure, callback failure, and nonallocating fallback diagnostics;
it is a larger API change. Another explicit policy is that callback exceptions are fatal
contract violations, but that still does not justify swallowing unrelated allocation failures.

**Completion check:** extend the existing allocation-failure tests to connected read/timer/ping
and close initiation, and async sends if introduced. Verify terminal state, cleanup, event
cardinality, and whether `io_context::run()` propagates under the selected contract. Existing
`close() noexcept` allocation-guard and queued-completion coverage must continue to pass.

<a id="ws-09"></a>

**WS-09. Optional ownership and dependency changes**

**Source TODOs covered:**

- [`WS-TRANSPORT-OPTIONS`](web_socket.cpp) — Test various connection options (different TLS levels, no SSL?).

These decisions can close TODOs with a documented rationale; implementation is not required
merely because an alternative exists.

| TODO / question | Options and recommendation |
| --- | --- |
| Hide Boost behind Nomisma wrappers | Alternate communication backends and simulation now provide a concrete use case. Follow the engine/executor/timer and event-source proposal in [current_work.md](current_work.md). Direct Boost dependencies currently belong to application/test consumers; migrate those consumers to neutral contracts when the wrappers exist. Preserve module and installed-package coverage. |
| Different TLS levels / no TLS | Test the intended supported TLS versions against the local server, retaining certificate and hostname verification. Treat plain `ws://` as a separate transport feature requiring a real consumer; it is not achieved by simply accepting another URL scheme. |

Received text/binary type and structured peer close code/reason are additional API decisions
not covered by the current TODOs. Preserve useful close diagnostics during batch 2; introduce
public fields only when callers need them.

**Coverage and execution notes**

The current test sources cover construction/URL integration, replacement while resolving,
repeated close/destruction, two allocation-failure cases, TLS echo/push, fragmentation with
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
| [`WS-PARAMS`](web_socket.cppm) | Review parameter contracts and validation. | [WS-04](#ws-04) |
| [`WS-PING`](web_socket.cppm) | Enforce passive Ping and active Pong deadlines. | [WS-06](#ws-06) |
| [`WS-IDLE`](web_socket.cppm) | Review the receive watchdog timer and test it properly (perhaps with a local WebSocket server). | [WS-06](#ws-06) |
| [`WS-OP-TIMEOUT`](web_socket.cppm) | Review async operation timeouts; reproduce a case when a timeout is triggered. | [WS-06](#ws-06) |
| [`WS-LARGE-MESSAGES`](web_socket.cppm) | Test sending and receiving really big messages (e.g. a huge Coinbase initial snapshot); distinguish socket buffers from message limits. | [WS-05](#ws-05) |
| [`WS-INIT`](web_socket.cppm) | Introduce init (a session should start in init, not in disconnected). | [WS-02](#ws-02) |
| [`WS-FAILURE-NAME`](web_socket.cppm) | Rename failed to conn_failed. | [WS-02](#ws-02) |
| [`WS-TERMINAL-FAILURE`](web_socket.cppm) | Make conn_failed terminal, on the same level as disconnected. | [WS-02](#ws-02) |
| [`WS-DETACH`](web_socket.cppm) | Any terminal state (disconnected, conn_failed) should also be detached from the parent. | [WS-02](#ws-02); retired — retain the parent link and use terminal status. |
| [`WS-CABLE-LOSS`](web_socket.cppm) | Reproduce hard disconnection (unplug the network cable). | [WS-06](#ws-06) |
| [`WS-INTERFACE-LOSS`](web_socket.cppm) | Reproduce Docker-based interface disconnection (a soft disconnection of a virtual interface may only be caught by the receive watchdog timer). | [WS-06](#ws-06) |
| [`WS-PEER-CLOSE`](web_socket.cppm) | Reproduce normal server disconnection (e.g. Binance resets the MD connection once per day). | [WS-06](#ws-06) |
| [`WS-SEND-REENTRY`](web_socket.cpp) | Try sending from inside the ws_data receiver handler to reproduce re-entry into send. | [WS-03](#ws-03); closed — unconfirmed. |
| [`WS-EXCEPTIONS`](web_socket.cppm) | Consider making the interface noexcept for everything except the constructor. | [WS-08](#ws-08) |
| [`WS-CONNECT-DEADLINE`](web_socket.cpp) | Make the connection procedure time-bound (we can use default_async_operation_timeout_sec for it). | [WS-06](#ws-06) |
| [`WS-SETUP-STORAGE`](web_socket.cpp) | Dispose of state required only for connection setup after the connection is established (e.g. resolver). | [WS-02](#ws-02) |
| [`WS-TRANSPORT-OPTIONS`](web_socket.cpp) | Test various connection options (different TLS levels, no SSL?). | [WS-09](#ws-09) |
| [`WS-EAGER-SESSION`](web_socket.cpp) | Always create a session so web_socket::send (and others) need not check for a valid session_ pointer. | [WS-02](#ws-02) |
| [`WS-MOVE-COPY`](web_socket.cpp) | Consider enabling move/copy construction and assignment for web_socket (why are they disabled?). | [WS-02](#ws-02) |
| [`WS-TLS-OWNERSHIP`](web_socket.cpp) | Review why the TLS context is at the web_socket level (could it move to session?). | [WS-02](#ws-02) |

Batches 1 and 2 are complete, including the direct-header module boundary, state/lifetime
contracts, moves, TLS ownership, session-owned delegates, and setup-storage disposal.
`WS-DETACH` is retired in favor of retained parent links. WS-04 parameter validation and
inactive-only updates are also complete. Continue with message sizes and limits in
[WS-05](#ws-05); timer cleanup and broader exception guarantees remain in stages 6 and 8.
Contract checks and TODO maintenance accompany each stage.
Coordinate the timer/clock redesign in batch 6 with the shared execution proposal in
[current_work.md](current_work.md).
