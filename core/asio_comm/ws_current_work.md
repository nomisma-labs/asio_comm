**WebSocket review and proposed work — 2026-10-31**

The broader execution, timer-wrapper, alternate-backend, and simulation design is proposed in
[current_work.md](current_work.md). This document tracks WebSocket-specific cleanup and fixes.

This report reviews the [interface](web_socket.cppm), [implementation](web_socket.cpp), and their TODOs,
the [component contract](../../docs/web_socket.md), and the existing unit and integration
test sources. It also revisits the issue review in the conversation; no separate issue-report
file was found in the repository. Findings below come from source inspection and dependency
documentation. Completed stages below include their build, test, and fault-reproduction evidence.

Mechanical cleanup in WS-01 and the state/lifetime work in WS-02 are complete.
Parameter validation and update rules in WS-04 are next. Each stage includes its contract
documentation and focused tests; building a general failure test framework is not a
prerequisite for the work.

| Stage ID | Batch | Complexity | Result |
| --- | --- | --- | --- |
| [WS-01](#ws-01) | Mechanical source cleanup and public-export audit | Small | Consistent code and justified public names |
| [WS-02](#ws-02) | State names, lifetime, and terminal cleanup | Medium–high; split into small steps | Predictable status and ownership transitions |
| [WS-04](#ws-04) | Parameter validation and update rules | Medium | Settings with defined values and application times |
| [WS-05](#ws-05) | Large messages and payload limits | Medium | Verified snapshot handling and bounded message size |
| [WS-06](#ws-06) | Heartbeats, deadlines, and connection-loss detection | Medium–high | Defined liveness behavior with reproducible tests |
| [WS-07](#ws-07) | Sending and backpressure | High | Responsive executor under slow or failed writes |
| [WS-08](#ws-08) | Exception guarantees | Medium–high | Reliable cleanup when operation initiation fails |
| [WS-09](#ws-09) | Optional ownership and dependency changes | Variable | Changes justified by actual requirements |

Stage IDs remain stable when stages are removed; the remaining stages are not renumbered.
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

**Next:** [WS-04](#ws-04) for parameter validation and update rules. Timer cleanup and peer-close
protocol handling continue in [WS-06](#ws-06), sending/reentrancy in [WS-07](#ws-07), and broader exception
guarantees in [WS-08](#ws-08). Detailed behavior is in the [component contract](../../docs/web_socket.md).

<a id="ws-04"></a>

**WS-04. Parameter validation and update rules**

**Source TODOs covered:**

- [`WS-PARAMS`](web_socket.cppm) — Review parameter contracts and validation.

Relevant code: `ws_params`, `apply_socket_options()`, timer setup, and the handshake decorator.

Confirmed issues: zero `ping_interval_sec` creates immediate timer deadlines; with no tracked
activity, repeated idle callbacks can run until the whole-second idle comparison trips.
Socket buffer settings are unsigned but are passed to Asio options accepting `int`, with no
range validation. Changing `enable_outbound_ping` after handshake does not start or stop the
ping timer, although it changes whether received pongs count as activity.

Choose the configuration contract before adding validation:

- **Option A — retain live reads:** smallest compatibility change. Validate values whenever
  consumed and document when each field applies. Existing tests intentionally exercise edits
  after `connect()` and before the handshake.
- **Option B — snapshot settings per connection:** simpler session reasoning and one initial
  validation point, but changes tested behavior and copies headers/settings. Use this only as
  an explicit API decision.
- **Option C — explicit runtime setters:** suitable if live heartbeat reconfiguration is
  required. Setters validate and rearm/cancel the affected operations; this adds public API.

Start with A. Reject zero timer intervals unless a setting explicitly defines zero as
disabled. Define accepted socket-buffer values, validate representability, and keep OS
rejection separate from invalid input. Use chrono durations internally; changing all public
fields to duration types can remain a later API choice.

**Completion check:** invalid values produce a predictable result; oversized buffer requests
do not silently narrow; retained/reset parameters and current live-header tests still work.
Validate at the points that actually consume mutable settings, not only in `connect()`.

<a id="ws-05"></a>

**WS-05. Large messages and payload limits**

**Source TODOs covered:**

- [`WS-LARGE-MESSAGES`](web_socket.cppm) — Test sending and receiving really big messages (e.g. a huge Coinbase initial snapshot); distinguish socket buffers from message limits.

Relevant code: `buffer_`, `on_receive()`, `send()`, and the large-message TODO.

There is no demonstrated 64 KiB incoming-message ceiling in this implementation. Reads assemble
messages in a dynamic `beast::flat_buffer`. The installed Beast implementation defaults to a
16 MiB incoming-message limit, consistent with its [message-size documentation](https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/ref/boost__beast__websocket__stream/read_message_max/overload1.html).

**Options:** document and retain that default, or expose a bounded `max_message_size` parameter
and apply it to the stream. Prefer the explicit setting if feeders have different payload
requirements. Do not use an effectively unlimited value as the first response to a large
snapshot. Keep socket-buffer tuning independent.

Extend the [loopback frame helpers](../../tests/integration/web_socket/loopback_test.py): the
current reader asserts a maximum frame size of 65,536 bytes and the tests mostly use small
messages. A large message may also span many frames, so the test server must validate complete
messages rather than assume one frame per send.

Test both directions above 64 KiB, fragmentation with interleaved control frames, and payloads
just below/at/above a configurable message limit. Use a small configured limit for most boundary
tests and one realistically large payload. Check that rejected input causes one terminal result
and partial payloads are not delivered as complete messages. Preserve the documented borrowed
payload lifetime. Avoid automatically shrinking the receive buffer after every message;
measure retained capacity before adding an allocation policy.

**Completion check:** complete payloads match byte-for-byte and limits behave predictably.
Successful large sends to a reading peer can be tested here; stalled large sends belong to
batch 7. Slow fragmented receives also feed the liveness tests in batch 6.

<a id="ws-06"></a>

**WS-06. Heartbeats, deadlines, and connection-loss detection**

**Source TODOs covered:**

- [`WS-PING`](web_socket.cppm) — Understand different types of pings and why we need enable_outbound_ping.
- [`WS-IDLE`](web_socket.cppm) — Review the idle timer and test it properly (perhaps with a local WebSocket server).
- [`WS-OP-TIMEOUT`](web_socket.cppm) — Review async operation timeouts; reproduce a case when a timeout is triggered.
- [`WS-CONNECT-DEADLINE`](web_socket.cpp) — Make the connection procedure time-bound (we can use default_async_operation_timeout_sec for it).
- [`WS-CABLE-LOSS`](web_socket.cppm) — Reproduce hard disconnection (unplug the network cable).
- [`WS-INTERFACE-LOSS`](web_socket.cppm) — Reproduce Docker-based interface disconnection (a soft disconnection of a virtual interface may only be caught by the idle timer).
- [`WS-PEER-CLOSE`](web_socket.cppm) — Reproduce normal server disconnection (e.g. Binance resets the MD connection once per day).

**Lifecycle follow-up from WS-02 (no separate source TODO):** make idle/ping cancellation
consistent across peer close, read errors, explicit close, and graceful-close completion.
In particular, peer-close/read-error paths can leave a ping wait pending until it expires.

**WIP:** application connection deadlines have been removed from all examples, including the
WebSocket example's overall response timer that also covered connection setup. An overall
connection deadline belongs to the client timeout work here. Test-harness deadlines remain
to bound test execution, and the OKEx application heartbeat remains a protocol requirement.
The removal passed the GCC `complete` build and the unit, local TLS loopback, and exchange-feeder
test suites. No new tests were added for this simplification.

Relevant code: `on_control()`, `start_idle_timer()`, `on_idle_timer()`, ping handlers, and
the resolve/connect/handshake chain.

Current behavior confirmed by inspection:

- A quiet but healthy peer can be disconnected even with outbound pings disabled.
- Received ping frames count as activity; pongs count only with outbound pings enabled.
- Data activity is recorded at complete-message delivery, so a slow fragmented message can
  be making network progress without refreshing `last_recv_ts_`.
- With defaults, idle checks run every 30 seconds and compare whole elapsed seconds against
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

- Separate the inactivity deadline from the outbound-ping interval; define whether and how
  inactivity checking can be disabled independently of ping sending.
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
that idle monitoring currently runs regardless of the outbound-ping flag. TCP keepalive,
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

<a id="ws-07"></a>

**WS-07. Sending and backpressure**

**Source TODOs covered:**

- [`WS-SEND-REENTRY`](web_socket.cpp) — Try sending from inside the ws_data receiver handler to reproduce re-entry into send.

Relevant code: `session::send()`, `on_ping_timer()`, the read loop, and `shutdown()`.

Confirmed limitation: synchronous `stream_.write()` can block the executor, delaying reads,
timers, and other connections on it. A timer on that executor cannot enforce a deadline while
the write is blocked. Mixed synchronous writes and pending asynchronous control writes remain
a correctness risk requiring targeted validation; this review does not claim a reproduced race.

**Options:**

- **Retain blocking `send()`:** smallest API change, but accepts the responsiveness limitation
  and requires an explicit supported-operation contract. Serializing handlers alone does not
  resolve overlap between pending operations.
- **One asynchronous send in flight, reject when busy:** smallest nonblocking design if callers
  can retain and retry messages. The API must distinguish busy from disconnected/failure.
- **Bounded asynchronous FIFO:** recommended if feeders need to send bursts. Own payloads until
  completion, bound queued bytes/messages, and report overload predictably.

Beast coordinates asynchronous reads, writes, ping/pong, and close, supporting one operation
from each category concurrently. Keep one data write in flight and use that coordination;
do not serialize the entire read/write lifecycle behind a single application queue.
Caller-side message buffering/backpressure is an application responsibility. [Beast operation rules](https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/using_websocket/notes.html).

Decide the observable send contract before implementation. Today's `true` means the blocking
write completed, not that the peer application processed it. An asynchronous API may report
acceptance immediately and completion separately. Do not silently reinterpret the existing
boolean. Choose whether queued messages are drained or canceled on graceful close, reject
new sends after closing starts, and prevent a reconnect from sending old-session messages.
Preserve the existing serialized-executor requirement; adding a queue does not make calls
from arbitrary threads safe.
Keep borrowed receive-payload and callback-reference lifetimes explicit during reentrancy
work. A delegate must not replace its own callable storage during invocation; document when
replacement must be posted until after the callback returns.

**Completion check:** send from a `ws_data` callback and verify the outcome while the read
loop and control traffic are active. Distinguish this callback-driven send from overlapping
write operations; neither case is presumed safe or broken without validation. Also cover a
slow/non-reading peer, sustained sends, queue saturation or busy rejection, ping/control traffic
during writes, write failure, close/reconnect with pending data, ordered delivery, payload
lifetime, and timer progress. Migrate all affected examples/feeders together.

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
ping/pong, peer close, partial-frame EOF, certificate/hostname rejection, live headers and agent
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
| [`WS-PING`](web_socket.cppm) | Understand different types of pings and why we need enable_outbound_ping. | [WS-06](#ws-06) |
| [`WS-IDLE`](web_socket.cppm) | Review the idle timer and test it properly (perhaps with a local WebSocket server). | [WS-06](#ws-06) |
| [`WS-OP-TIMEOUT`](web_socket.cppm) | Review async operation timeouts; reproduce a case when a timeout is triggered. | [WS-06](#ws-06) |
| [`WS-LARGE-MESSAGES`](web_socket.cppm) | Test sending and receiving really big messages (e.g. a huge Coinbase initial snapshot); distinguish socket buffers from message limits. | [WS-05](#ws-05) |
| [`WS-INIT`](web_socket.cppm) | Introduce init (a session should start in init, not in disconnected). | [WS-02](#ws-02) |
| [`WS-FAILURE-NAME`](web_socket.cppm) | Rename failed to conn_failed. | [WS-02](#ws-02) |
| [`WS-TERMINAL-FAILURE`](web_socket.cppm) | Make conn_failed terminal, on the same level as disconnected. | [WS-02](#ws-02) |
| [`WS-DETACH`](web_socket.cppm) | Any terminal state (disconnected, conn_failed) should also be detached from the parent. | [WS-02](#ws-02); retired — retain the parent link and use terminal status. |
| [`WS-CABLE-LOSS`](web_socket.cppm) | Reproduce hard disconnection (unplug the network cable). | [WS-06](#ws-06) |
| [`WS-INTERFACE-LOSS`](web_socket.cppm) | Reproduce Docker-based interface disconnection (a soft disconnection of a virtual interface may only be caught by the idle timer). | [WS-06](#ws-06) |
| [`WS-PEER-CLOSE`](web_socket.cppm) | Reproduce normal server disconnection (e.g. Binance resets the MD connection once per day). | [WS-06](#ws-06) |
| [`WS-SEND-REENTRY`](web_socket.cpp) | Try sending from inside the ws_data receiver handler to reproduce re-entry into send. | [WS-07](#ws-07) |
| [`WS-EXCEPTIONS`](web_socket.cppm) | Consider making the interface noexcept for everything except the constructor. | [WS-08](#ws-08) |
| [`WS-CONNECT-DEADLINE`](web_socket.cpp) | Make the connection procedure time-bound (we can use default_async_operation_timeout_sec for it). | [WS-06](#ws-06) |
| [`WS-SETUP-STORAGE`](web_socket.cpp) | Dispose of state required only for connection setup after the connection is established (e.g. resolver). | [WS-02](#ws-02) |
| [`WS-TRANSPORT-OPTIONS`](web_socket.cpp) | Test various connection options (different TLS levels, no SSL?). | [WS-09](#ws-09) |
| [`WS-EAGER-SESSION`](web_socket.cpp) | Always create a session so web_socket::send (and others) need not check for a valid session_ pointer. | [WS-02](#ws-02) |
| [`WS-MOVE-COPY`](web_socket.cpp) | Consider enabling move/copy construction and assignment for web_socket (why are they disabled?). | [WS-02](#ws-02) |
| [`WS-TLS-OWNERSHIP`](web_socket.cpp) | Review why the TLS context is at the web_socket level (could it move to session?). | [WS-02](#ws-02) |

Batches 1 and 2 are complete, including the direct-header module boundary, state/lifetime
contracts, moves, TLS ownership, session-owned delegates, and setup-storage disposal.
`WS-DETACH` is retired in favor of retained parent links. Continue with parameter validation
and update rules in [WS-04](#ws-04); timer cleanup and broader exception guarantees remain
in stages 6 and 8. Contract checks and TODO maintenance accompany each stage.
Coordinate the timer/clock redesign in batch 6 with the shared execution proposal in
[current_work.md](current_work.md).
