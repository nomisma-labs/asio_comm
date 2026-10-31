**WebSocket review and proposed work — 2026-10-31**

This report reviews the current [implementation](web_socket.cppm), its six TODO blocks,
the [component contract](../../docs/web_socket.md), and the existing unit and integration
test sources. It also revisits the issue review in the conversation; no separate issue-report
file was found in the repository. Findings below come from source inspection and dependency
documentation. No build, test run, or fault reproduction was performed for this report.

The order deliberately starts with cleaning. Batches 1–2 can be completed before changing
runtime behavior. Later batches include their own focused tests; building a general failure
test framework is not a prerequisite for the cleanup.

| Order | Batch | Complexity | Result |
| --- | --- | --- | --- |
| 1 | Mechanical source cleanup | Very small | Consistent, easier-to-read code |
| 2 | Contract and TODO cleanup | Small | Accurate explanations and explicit open decisions |
| 3 | State names, lifetime, and terminal cleanup | Medium–high; split into small steps | Predictable status and ownership transitions |
| 4 | Parameter validation and update rules | Medium | Settings with defined values and application times |
| 5 | Large messages and payload limits | Medium | Verified snapshot handling and bounded message size |
| 6 | Heartbeats, deadlines, and connection-loss detection | Medium–high | Defined liveness behavior with reproducible tests |
| 7 | Sending and backpressure | High | Responsive executor under slow or failed writes |
| 8 | Exception guarantees | Medium–high | Reliable cleanup when operation initiation fails |
| 9 | Optional ownership and dependency changes | Variable | Changes justified by actual requirements |

Complexity describes implementation and validation effort, not severity. Blocking writes
are a significant issue even though their redesign comes after the requested cleaning work.
Batch 8 uses the common cleanup from batch 3; exception handling for newly changed paths
belongs in those changes immediately rather than waiting for the final exception audit.

**1. Mechanical source cleanup**

The first change should be small enough to review without reasoning about the state machine.

- Remove the second declarations of `net` and `ssl`; each alias only needs one definition.
- Use `net::steady_timer` consistently for the timer members and `std::size_t` for the
  byte count in `send()`.
- Normalize TODO indentation, punctuation, and `//...` comments.
- Remove comments that merely repeat the adjacent statement. Keep comments explaining
  cancellation, borrowed data, callback lifetime, and valid handler entry states.
- Keep the GCC module-boundary explanation: it documents a concrete integration constraint.

Avoid extracting generic helpers or changing switch structure just to reduce line count.
The explicit state cases are useful when reviewing asynchronous completions.

**Completion check:** inspect the diff for semantic changes, run formatting and a development
build. New tests are unnecessary for purely mechanical edits.

**2. Contract and TODO cleanup**

This is a documentation change that turns broad questions into independently actionable work.

- Correct the large-message TODO: socket receive-buffer capacity, assembled-message storage,
  and maximum accepted WebSocket message size are different limits. Do not diagnose the
  reported exchange snapshot problem as a 64 KiB limit without a failing payload or trace.
- Explain the existing timeout precisely: one deadline starts before TCP connect and remains
  active through TLS; DNS is outside it. WebSocket handshake and close use Beast's defaults.
- Explain that outbound ping is optional, automatic replies to peer pings are separate,
  and the idle timer currently runs regardless of the outbound-ping flag.
- Record that parameters are read at different times. Editing the struct does not immediately
  reconfigure the stream or start/stop timers.
- State that `close()` aborts and suppresses events, while the private `shutdown()` requests
  a graceful WebSocket close. Similar names currently obscure different contracts.
- Resolve the documentation contradiction about reconnecting inside a delegate: the current
  implementation replaces that delegate. Until callback lifetime is addressed, document
  reconnect/delegate replacement after the active callback returns.
- Keep design questions marked as decisions. Eager session creation, moving the wrapper,
  moving the TLS context, and hiding Boost are not established bug fixes.

The repeated `expires_never()` in `on_handshake()` is a candidate for a separate small
simplification: the TCP deadline was already disabled in `on_ssl_handshake()`. Verify that
no path re-enables it before removing the call and its exception-reporting branch. This is
not just whitespace cleanup, because it removes a setup/error path.

**Completion check:** source and docs agree; every TODO maps to a batch below; existing
constraints remain visible. Use local links and symbol names so the report survives line shifts.

**3. State names, lifetime, and terminal cleanup**

Relevant code: [status TODO](web_socket.cppm#L119), `session::close()`, the connection
completion handlers, `on_control()`, `on_receive()`, and `on_shutdown()`.

Confirmed behavior: `failed` already ends the connection attempt; sessions are never retried
in place. The missing part of the TODO is consistent detachment and resource cleanup.
Connection failures can leave the transport owned by the wrapper until explicit close or
replacement. Peer-close/read-error paths cancel the idle timer but can leave a ping wait
pending, keeping executor work alive until it expires. These are cleanup gaps, not evidence
of an inevitable use-after-free: existing status guards suppress later delegate calls.

Implement this batch as three reviewable steps:

1. Introduce `init` for a fresh session and rename `failed` to `conn_failed`. Update enum users,
   logs, tests, and the state diagram. Treat this as an API change, not cosmetic renaming.
2. Introduce one terminal-finalization policy shared by connection failure and disconnection.
   Preserve the selected terminal status, detach notification ownership, cancel outstanding
   work, and release transport resources when the protocol operation permits it.
3. Apply the policy to every completion/error path and test event counts, status visibility,
   cancellation, and reentrancy.

**Public `init` options:** keep the wrapper reporting `disconnected` when it has no session,
or expose `init` before its first connection. The latter is clearer if callers need to
distinguish “never started”; implement it with explicit wrapper state rather than allocating
a resolver/TLS stream solely for the status getter. Adding `init` only to `session` does not
change the current no-session result of `web_socket::status()`.

**Detachment options:** clear `session::parent_` while retaining the terminal session in the
wrapper, or release the wrapper's `session_` as well and store terminal status/identity
separately. Start with the first option: it preserves diagnostics with fewer moving parts.
Specify detachment direction explicitly. Cleanup must not accidentally turn `conn_failed`
into `disconnected`, as the current general-purpose `session::close()` would do.
Update completion guards for both terminal states: once a timeout or other failure cancels
pending work, a late completion can legitimately arrive in `conn_failed`. The current
connection-stage assertions assume only `connecting` or `disconnected` can be observed.

**Callback options:** require reconnect/delegate replacement to be posted until after the
callback returns, or retain stable callback storage throughout invocation so replacement is
safe. The first is the smaller change. The second supports direct callback-driven reconnect
but needs a lifetime design; copying every `std::function` on the data path introduces cost
and possible allocation failure. Save the notification context before detaching, and do not
access an old owner after user code returns without the required lifetime/state checks.

Do not finalize peer close by blindly calling `close()` from `on_control()`. That callback
runs inside a read operation, and Beast performs the protocol response and teardown.
Prefer recording the peer reason there and completing the terminal transition at read/close
completion. Keep the read needed by graceful shutdown alive. [Beast control-callback contract](https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/ref/boost__beast__websocket__stream/control_callback/overload1.html).

**Completion check:** cancel at each connection stage; fail each handshake; close repeatedly;
drain queued completions; disconnect with a ping wait outstanding; exercise callback-triggered
close and the selected reconnect policy. Verify one terminal event where promised, none after
explicit abort, preserved failure status, and no new work from an obsolete session. Extend the
existing loopback server only as needed for these cases.

**4. Parameter validation and update rules**

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

**5. Large messages and payload limits**

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

**6. Heartbeats, deadlines, and connection-loss detection**

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

**Heartbeat options:**

- **Use Beast idle timeout and keep-alive pings.** Preferred if the required contract is
  “probe an idle peer and close if it remains unresponsive.” This can remove custom timers.
- **Keep explicit timers.** Appropriate if exchanges require fixed ping cadence, matching
  pong payloads, or independent heartbeat rules. Separate idle timeout from ping interval;
  compare chrono durations directly and define the activity being tracked.

Beast supports idle-timeout and automatic idle-ping settings, and WebSocket timeout support
requires asynchronous I/O. Its handshake timeout also covers close. Keep TCP deadlines
disabled after control passes to the WebSocket stream. [Beast timeout contract](https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/using_websocket/timeouts.html).

TCP keepalive, WebSocket control frames, and exchange JSON/text heartbeats serve different
purposes. Keep exchange-specific messages in the feeder/protocol layer. Do not add pong matching
if the actual requirement is merely to observe any peer activity.

**Connection-timeout options:** one overall attempt budget covering DNS through WebSocket
upgrade, or separate stage budgets. Prefer one overall budget initially, with the failing
stage included in diagnostics; add stage controls only when required. A separate attempt
timer can cancel the active work without trying to reuse the TCP deadline for WebSocket I/O.
Define a bounded close policy too: detection time plus graceful-close time determines when
the caller observes final disconnection. If the transport is already failed, consider aborting
directly rather than trying a normal close handshake.

**Completion check:** local tests for idle-but-responsive peer, ignored pings, unsolicited
pongs, slow fragmented data, stalled TLS/upgrade, EOF, reset, peer close, and a peer that does
not answer close. Use bounded timing ranges and verify the event reason. Reproduce DNS and TCP
blackholes with controlled infrastructure or a narrow test seam; an unreachable address alone
does not reliably reproduce a timeout. Add optional network-namespace/Docker fault tests for
packet drops and interface loss, and a manual cable-removal recipe. A reset test does not prove
blackhole detection. Simulate periodic server close locally rather than waiting for an exchange.

**7. Sending and backpressure**

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

**Completion check:** slow/non-reading peer, sustained sends, queue saturation or busy rejection,
ping/control traffic during writes, write failure, close/reconnect with pending data, ordered
delivery, payload lifetime, and timer progress. Migrate all affected examples/feeders together.

**8. Exception guarantees**

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
`close() noexcept` cancellation-failure coverage must continue to pass.

**9. Optional ownership and dependency changes**

These decisions can close TODOs with a documented rationale; implementation is not required
merely because an alternative exists.

| TODO / question | Options and recommendation |
| --- | --- |
| Always create a session | Keep lazy transport creation, or keep an always-present lightweight session with lazy transport resources. Prefer current lazy creation unless the new state model becomes materially simpler. Eliminating a few null checks alone is insufficient justification. |
| Enable move construction/assignment | Keep a stable wrapper address, or explicitly rebind the active session's raw `parent_` and define pending-callback/move-assignment behavior. Keep moves disabled until a caller needs them. Defaulted move is not safe for this ownership model; the TLS context's move behavior is not the only concern. |
| Enable copying | Keep disabled. Copying an active connection has no defined meaning. A separate configuration object can be copied if constructing similar clients is the actual requirement. |
| Move `ssl_ctx_` into the session | Wrapper ownership reuses trust configuration across attempts; session ownership gives each attempt its own context; shared immutable TLS configuration is another option if many clients need it. Keep the current placement until ownership, configuration, or measured cost favors a change. No context-lifetime defect was established here. |
| Dispose connection-only state | Measure resolver and handshake-storage retention first. If worthwhile, use a small optional connection-setup object and release it after the final handshake operation no longer needs it. Keep cancellation valid throughout setup. |
| Hide Boost behind Nomisma wrappers | Retain the narrow aliases, or introduce actual execution/timer/error abstractions. Aliases do not hide dependency types. Prefer aliases until there is a defined portability/API boundary; do not build a general executor framework solely for this client. Preserve the module-consumer workaround and integration coverage. |
| Different TLS levels / no TLS | Test the intended supported TLS versions against the local server, retaining certificate and hostname verification. Treat plain `ws://` as a separate transport feature requiring a real consumer; it is not achieved by simply accepting another URL scheme. |

Received text/binary type and structured peer close code/reason are additional API decisions
not covered by the current TODOs. Preserve useful close diagnostics during batch 3; introduce
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

| Source TODO block | Disposition |
| --- | --- |
| Boost type wrappers | Batch 9: optional abstraction decision |
| Parameters: ping, idle, operation timeouts, big messages | Batches 2, 4, 5, and 6 |
| `init`, `conn_failed`, terminal states, detachment | Batch 3 |
| Connection-loss scenarios and `noexcept` | Batches 6 and 8 |
| Connection-only storage and TLS/plain transport options | Batch 9; targeted TLS tests can be independent |
| Eager session, move/copy, TLS-context placement | Batch 9, coordinated with batch 3's ownership contract |

The first implementation deliverable should contain batch 1 only. Follow with batch 2, then
select the public-state and callback contracts for batch 3. This keeps the requested cleaning
phase concrete and leaves the substantive design decisions visible for discussion.
