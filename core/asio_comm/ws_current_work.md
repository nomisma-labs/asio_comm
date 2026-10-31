**WebSocket review and proposed work — 2026-10-31**

The broader execution, timer-wrapper, alternate-backend, and simulation design is proposed in
[current_work.md](current_work.md). This document tracks WebSocket-specific cleanup and fixes.

This report reviews the [interface](web_socket.cppm), [implementation](web_socket.cpp), and their TODOs,
the [component contract](../../docs/web_socket.md), and the existing unit and integration
test sources. It also revisits the issue review in the conversation; no separate issue-report
file was found in the repository. Findings below come from source inspection and dependency
documentation. Completed stages below include their build, test, and fault-reproduction evidence.

Mechanical cleanup in WS-01 and the state/lifetime work in WS-02 are complete.
Contract and TODO cleanup follows in WS-03. Behavior changes
include their own focused tests; building a general failure test framework is not a
prerequisite for the cleanup.

| Stage ID | Batch | Complexity | Result |
| --- | --- | --- | --- |
| [WS-01](#ws-01) | Mechanical source cleanup and public-export audit | Small | Consistent code and justified public names |
| [WS-02](#ws-02) | State names, lifetime, and terminal cleanup | Medium–high; split into small steps | Predictable status and ownership transitions |
| [WS-03](#ws-03) | Contract and TODO cleanup | Small | Accurate explanations and explicit open decisions |
| [WS-04](#ws-04) | Parameter validation and update rules | Medium | Settings with defined values and application times |
| [WS-05](#ws-05) | Large messages and payload limits | Medium | Verified snapshot handling and bounded message size |
| [WS-06](#ws-06) | Heartbeats, deadlines, and connection-loss detection | Medium–high | Defined liveness behavior with reproducible tests |
| [WS-07](#ws-07) | Sending and backpressure | High | Responsive executor under slow or failed writes |
| [WS-08](#ws-08) | Exception guarantees | Medium–high | Reliable cleanup when operation initiation fails |
| [WS-09](#ws-09) | Optional ownership and dependency changes | Variable | Changes justified by actual requirements |

Stage IDs `WS-01`–`WS-09` are stable references to the numbered batches below. Source comments use
`TODO[WS-…] (stage WS-…)`; each TODO ID identifies one concern and stays unchanged if work is
reordered. Search either source file for the exact TODO ID. The lists at the start of each stage
link to its source file; the index below also links each TODO back to its stages. Shared TODOs
appear in both the contract-cleanup stage and the implementation stage; listing one does not
mean it is completed. Findings without a source TODO are identified explicitly.

Complexity describes implementation and validation effort, not severity. Blocking writes
are a significant issue even though their redesign comes after the requested cleaning work.
Batch 8 uses the common cleanup from batch 2; exception handling for newly changed paths
belongs in those changes immediately rather than waiting for the final exception audit.

<a id="ws-01"></a>

**WS-01. Mechanical source cleanup**

**Source TODOs covered:** none remaining; the export audit is complete.

**Completed, including direct Boost consumers and module-boundary repair — 2026-10-31.**
Removed duplicate aliases, standardized type spellings, and
normalized TODO/comment formatting while retaining lifetime and state-invariant explanations.
Final validation for the repaired boundary is recorded below. Bootstrap's Git hook/dependency
setup requires writable Git metadata; validation used the direct CMake build/test presets.

The first change should be small enough to review without reasoning about the state machine.

- Remove the second declarations of `net` and `ssl`; each alias only needs one definition.
- Use `net::steady_timer` consistently for the timer members and `std::size_t` for the
  byte count in `send()`.
- Normalize TODO indentation, punctuation, and `//...` comments.
- Remove comments that merely repeat the adjacent statement. Keep comments explaining
  cancellation, borrowed data, callback lifetime, and valid handler entry states.
- Keep Asio utility dependencies in their consumers; remove `nom::asio` utility re-exports.
- Separate backend definitions from the module interface so direct Asio includes work in consumers.

Avoid extracting generic helpers or changing switch structure just to reduce line count.
The explicit state cases are useful when reviewing asynchronous completions.

**Completion check:** inspect the diff for semantic changes, run formatting and a development
build. New tests are unnecessary for purely mechanical edits.

**Public dependency boundary.** Example apps and test harnesses include the Boost headers
they use and declare `Boost::headers` as a direct target dependency. They use explicit Boost
types, including timer callback error arguments. Application utility requirements do not
justify exporting those types from the WebSocket module. No `nom::asio` utility aliases remain.

| Type | Current use | Dependency owner |
| --- | --- | --- |
| `boost::asio::io_context` | Application event loop; unit/integration-test execution harness. | Each application/test consumer. |
| `boost::asio::any_io_executor` | WebSocket constructor parameter. | Explicit Boost API dependency until the neutral executor proposal is implemented; the interface uses forward declarations. |
| `boost::asio::steady_timer` | OKEx text heartbeat and integration-test deadlines. | The application/test consumer; application connection deadlines have been removed. |
| `boost::system::error_code` | Explicit argument type in the OKEx heartbeat and integration-test timer callbacks. | The consumer's Boost callback API; no generic lambda is needed to avoid an export. |

Representative callers: [WebSocket example](../../apps/web_socket_example/main.cpp),
[Binance perpetual feeder](../../apps/binance_perp_feeder/main.cpp), and
[OKEx feeder](../../apps/okex_spot_feeder/main.cpp).

**Module boundary fix.** Direct Asio includes reproduced GCC 16 assembler errors for duplicate
`prefer`, `require`, and `query` symbols while the full backend lived in the interface unit.
The [module interface](web_socket.cppm) now contains forward declarations, public events,
parameters, and the socket facade. The [implementation unit](web_socket.cpp) contains Asio,
Beast, TLS, session handlers, and an opaque `io_state` holding the executor and TLS context.
Constructors and destruction are out of line. This adds one private-state allocation per
client and keeps session policy unchanged. It is a module-boundary repair, not the proposed
backend-neutral engine implementation.

The remaining Boost constructor type is deliberate: accepting a backend executor is distinct
from re-exporting application utilities. Replacing it with a neutral executor/timer contract is
proposed in [current_work.md](current_work.md).

**Boundary validation:** all applications and test clients build with GCC 16 (`complete`) and
Clang 22 (`clang`). All seven tests pass on each compiler across the full run and targeted
consumer reruns: unit, local TLS loopback, exchange feeders, example workflow, installation,
and source-tree/installed-package usage. Network tests ran with localhost socket access.
The standalone consumer explicitly discovers Boost for its direct dependency. Formatting,
SPDX headers, local links, and diff checks pass. The session body was checked against the
pre-split version; only accesses to executor/TLS storage changed.

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

Relevant code: [status TODO](web_socket.cppm), `session::close()`, the connection
completion handlers, `on_control()`, `on_receive()`, and `on_shutdown()`.

**Status — complete 2026-11-01:** all source TODOs in this stage are implemented or resolved;
`WS-DETACH` is retired. `WS-SETUP-STORAGE` completes the stage. The next stage is WS-03.
Timer-specific cleanup continues in WS-06, sending/reentrancy in WS-07, and broader exception
guarantees in WS-08.

**Progress — 2026-11-01:** `WS-EAGER-SESSION` is implemented. The wrapper constructs its first
session eagerly, starts it on the first `connect()`, retains its status/identity on `close()`,
and creates a fresh session for reconnect. Accessors and `send()` no longer check for a null
session. The user's `WS-INIT` and `WS-FAILURE-NAME` changes are incorporated. An unused wrapper
reports `init`; closing it is a no-op. Cancelling setup now reports `conn_failed`, which is
preserved by repeated close; only a session that reached `connected` can become `disconnected`.
Setup completion guards now ignore `conn_failed` instead of `disconnected`. Explicit close also
disables Beast handshake/close timeouts: retaining the stream must not leave its internal
timer keeping the executor alive after transport cancellation.
Each stateful session operation or handler uses one entry-state `switch`/`case` mapping. Mid-function
checks for state changes during delegate calls use concise `if (status_ ...)` guards. Cleanup and status/identity retention follow the same lifecycle contract.
The `web_socket` wrapper is a holder and forwarding interface, not another state machine. Its
`connect()` uses an existing `init` session directly; otherwise it closes and replaces it,
assuming only that the new session starts in `init`. It does not branch on post-close states.
The single-switch refinement passed the `complete` build and the unit, local TLS loopback,
and exchange-feeder suites.

**Ownership decision for this change:** retain `shared_ptr<session>`. Pending operations capture
`shared_from_this()` and can complete after close, reconnect, or wrapper destruction. A direct
`unique_ptr` replacement would invalidate the captured session/transport/buffers. Unique
ownership is possible with a separate shared operation state or an explicit drain/retirement
contract, but adds a lifetime mechanism that this API does not currently need. Each session
constructs its transport and timers eagerly; setup-only resources are allocated in `connect()`.

**Validation:** GCC 16 `complete` builds all applications and test clients; all seven tests
pass, including installation and source/installed-package consumers. Clang 22 AddressSanitizer
builds pass the unit, local TLS loopback, and exchange-feeder suites, with allocation-mismatch
and leak checking enabled. Tests cover the eager identity, first-connect reuse, retained
terminal state/identity, reconnect before draining old completions, destruction with handlers
either drained or discarded, and injected allocation failures. The allocation-failure fixture
now also pairs nothrow allocation with its existing malloc/free replacement. Network tests
and LeakSanitizer ran with the required sandbox access. Bootstrap setup could not write Git
metadata; direct CMake presets provided build/test validation. Formatting, SPDX headers, local
links, TODO references, and diff checks pass.

**Later-stage lifecycle work:** terminal states suppress obsolete completions through their
entry-state switches. Peer-close/read-error paths can leave a ping wait pending; review
consistent idle/ping cleanup in WS-06. Connected-operation initiation failures and cleanup
under exceptions belong to WS-08. These are separate from completed setup-storage cleanup.

**Terminal-state review — 2026-11-01:** `WS-TERMINAL-FAILURE` is complete as a state-machine
contract. `conn_failed` and `disconnected` both terminate their session; creating a replacement
session is distinct from changing the old session's terminal state.

| Path / operation | Reviewed behavior |
| --- | --- |
| DNS initiation throws in `session::connect()` | Set `conn_failed` before rethrowing; no delegate notification. |
| DNS, TCP, TLS, or WebSocket completion reports an error | Set `conn_failed`, emit `network_error`, and stop that handler without starting the next stage. |
| TCP/TLS/WebSocket setup throws; SNI setup fails; WebSocket completion setup throws | Select `conn_failed` before reporting failure; setup exceptions notify directly from the catches through a lambda using the retained parent link. |
| Explicit close while connecting | Select `conn_failed` and cancel; suppress delegate notifications. |
| Late connection-stage completion, including already queued success | The `conn_failed` case ignores it without dereferencing `parent_` or starting work. |
| Send or reconnect | Send rejects the terminal session; wrapper reconnect closes and replaces it. `session::connect()` itself starts only from `init`. |
| Repeated close or destruction | Preserve `conn_failed`; neither turns it into `disconnected`. |

Reads, control callbacks, idle/ping waits, ping sends, and graceful-close completions start
only after `connected` (or from its `disconnecting` phase). Their switches therefore do not
need a `conn_failed` case: that state is reachable only before the session ever connects.
Failures while connected select `disconnected`, so there is no connected-to-`conn_failed` edge.
Throws during diagnostic/event construction cannot change the terminal state back; notification
and cleanup guarantees under those exceptions remain part of WS-08.

**Retained parent decision — 2026-11-01:** `WS-DETACH` is retired. Every session retains its
parent address in every state; terminal status alone suppresses late completions. All runtime
state writes use `set_status<NewStatus>()`, which updates status without clearing the parent.
Debug checks require state transitions to advance in enum order, and selecting `init` after
construction is forbidden at compile time. Repeated close preserves the terminal state
without invoking a self-transition. The parent pointer does not encode status.

Connection handlers use a local notification lambda that reads the retained parent link.
Each catch selects `conn_failed` and invokes that lambda directly, without an intermediate
error string, saved owner pointer, or post-catch status test. The status change precedes
copying the error text, logging, and constructing the event. Exceptions from message
construction or a delegate in a catch body propagate out; another catch on the original try
does not handle them. Peer-close, read-error, and close-completion paths select `disconnected`
and notify directly through the retained parent link. Their later completions do nothing.
The wrapper remains a holder and requires no knowledge of these state transitions.

The tests exercise synchronous initiation failure, cancellation before resolution completion,
repeated close, rejected terminal sends, reconnect/destruction with old handlers, TLS
certificate rejection, a handshake setup exception, rejected WebSocket upgrades, and recovery.
They do not inject every SNI/setup failure or cancel separately at each TCP/TLS/upgrade stage.
Tests also cover moves involving terminal sessions and reuse of both wrappers afterward.

**Terminal-state validation:** the GCC 16 `complete` build and all seven tests pass.
The Clang 22 AddressSanitizer build and the unit, local TLS loopback, and exchange-feeder
suites pass, with leak and allocation-mismatch checks enabled. The upgrade-rejection fixture
checks one failure event, no connected event, rejected sends, preserved state/identity through
close, and recovery on a fresh attempt. Formatting, headers, Python checks, local links, TODO
references, and diff checks pass. Bootstrap setup still cannot write Git metadata; validation
used direct CMake presets, with localhost/LeakSanitizer access for the relevant tests.


Completed scope:

1. Keep an always-present session, reuse `init` for the first connect, and replace terminal
   sessions on reconnect while preserving their final status and identity.
2. Use `init` and `conn_failed`, with one entry-state switch per stateful session operation.
3. Centralize status transitions and setup-resource release. Suppress obsolete completions
   and retain the parent address in every state.
4. Implement moves, retain wrapper-owned TLS configuration, place delegates in sessions, and
   validate cancellation, callback lifetime, failure paths, and setup-storage disposal.

**Ownership and value-semantics decisions:** resolved together with the session/lifetime
contract in this stage.

| TODO / question | Decision |
| --- | --- |
| Enable move construction/assignment | **Complete.** Exchange fields and both parent pointers through `swap_state()`, then call `other.close()` on the former destination session. Move construction prepares a fresh source session to keep both wrappers nonempty; allocation failure leaves the source unchanged. |
| Enable copying | Keep disabled. Copying an active connection has no defined meaning. A separate configuration object can be copied if constructing similar clients is the actual requirement. |
| Move the TLS context into the session | **Resolved: retain it in `io_state`.** Configure once per wrapper and reuse across connection attempts. Its heap address stays stable when `io_` is transferred. Wrapper moves transfer the backend and session together. |
| Dispose connection-only state | **Complete.** Store the resolver and HTTP Host-header string in a uniquely owned setup object. Release it on transition to `connected` or `conn_failed`, before notifying the delegate. |

**TLS ownership decision — completed 2026-11-01:** `WS-TLS-OWNERSHIP` is done. Retain the
TLS context in `io_state`, configure it once per wrapper, and reuse it across connection
attempts. Keep ownership explicit rather than introducing thread-local storage.

The purpose of this review was to reduce
wrapper-owned state and simplify move semantics. WS-01 moved the executor and TLS context into
`web_socket::io_state` when the module interface and backend implementation were separated.
The wrapper still owns that object through `std::unique_ptr<io_state> io_`; it has not transferred
TLS ownership to `session`. Session streams are constructed using `parent_->io_->ssl_ctx`,
and trust configuration is initialized by the wrapper constructor.

The context already has a stable heap address, so transferring `io_` does not itself move the
TLS context object. Moving the TLS context into each session is therefore not a prerequisite
for wrapper move support. `WS-MOVE-COPY` updates each session's `parent_` when transferring
holders, preserving safe cancellation and the always-present-session rule.

**Move support — completed 2026-11-01:** `WS-MOVE-COPY` is complete. The
move constructor prepares a fresh `init` session for the source, then transfers the existing
state; allocation failure leaves the source unchanged. Move assignment exchanges fields and
session parent pointers first, then calls `other.close()` on the destination's former session.
The source retains the destination's former state after close. Copying remains deleted.
Moves are serialized with both wrappers and occur outside an active delegate invocation;
callback-requested moves are posted.

**Swap-first assignment refinement:** exchange the wrappers' fields and session parent
pointers, then call `other.close()`. The source owns the old destination session, so cleanup
applies to that session. Parent pointers are swapped directly because terminal transitions
retain them; the swap is independent of session status. Only `web_socket::swap_state` is a
friend of `session`. Tests include assignment over
an already terminal destination, construction from a terminal session followed by immediate
source reuse, and assignment of a terminal session over a pending attempt.

The GCC 16 `complete` build and all seven tests pass for move support. Clang 22
AddressSanitizer builds and the unit, local TLS loopback, and exchange-feeder suites pass.
Coverage includes unused/terminal moves, setup with queued completions, independent execution
contexts, self-move, failure to allocate a source replacement, and all four TLS fixture modes:
construction/assignment during setup and after connection with pending reads and heartbeats.
Formatting, headers, Python checks, local links, and diff checks pass. Bootstrap Git setup
remains unavailable in this sandbox; direct CMake presets supplied build/test evidence.

**Session-owned delegate — 2026-11-01:** the delegate is stored in `session`. Event handlers
invoke it directly, passing `*parent_` as the wrapper argument. The wrapper's `callbacks()`
and `update_delegate()` forward out of line to the current session. A replacement session
copies its callbacks before replacing the old holder, so `connect(client.callbacks())` is
valid. Moves transfer the session and its callback storage together; `swap_state()` exchanges
parameters, URL, backend, session, and parent pointers. Pending operations retain the old
session's delegate and captures until completion or destruction, while terminal-state guards
suppress obsolete notifications. Tests cover callback replacement during setup, borrowed
callbacks on reconnect, callback-capture lifetime, and callback routing after moves.

**Setup storage — completed 2026-11-01:** `WS-SETUP-STORAGE` is complete. `connect()` allocates
a `setup_state` containing the resolver and HTTP Host-header string after entering `connecting`,
inside the `try` block. Allocation failure selects `conn_failed` and propagates the original
exception; no event is emitted. The object remains available throughout connection setup.
`set_status<ws_status::connected>()` and
`set_status<ws_status::conn_failed>()` release it before any notification or further error construction.
The established session retains its stream, receive buffer, timers, identity, and delegate.

Destroying the setup object's resolver cancels outstanding resolution. `close()` first selects
`conn_failed`, so canceled or already queued resolve completions stop at the terminal-state
guard. TCP/TLS/upgrade completions likewise ignore a failed attempt before accessing setup
storage. The Host header remains available until the handshake completes, fails, or is
canceled. Reconnect constructs a fresh session and allocates setup when starting its attempt;
moves transfer the existing session and its current setup ownership together.

On the GCC 16 x86-64 build with Boost 1.90, the resolver occupies 80 bytes and `std::string` occupies
32 bytes. The setup object is 112 bytes; after it is released, the session retains only its
8-byte `unique_ptr` holder. Its heap storage, resolver cancellation state, and any allocated
Host-header capacity are released. Each started connection attempt adds one temporary setup
allocation. An unused `init` session has no setup allocation, including a move source's fresh replacement.

The allocation-guard test now verifies that closing pending setup neither allocates a new
resolver cancellation token nor delivers stale events. Existing loopback coverage exercises
successful connection, TLS rejection, handshake setup exceptions, rejected upgrades,
reconnect, and moves during setup/after connection. The fresh GCC `complete` build and all
seven tests pass. Clang ASan builds and all three selected suites (unit, TLS loopback, and
exchange feeders) pass. Formatting, SPDX headers, Python checks, local links, and diff checks
pass. Bootstrap cannot write Git metadata in this sandbox; direct CMake presets supplied
the fresh build and test validation.

**Public initial state:** implemented as `init` on the eager session. The wrapper reports that
state until its first attempt, including after repeated `close()` before connection setup.

**Parent lifetime:** explicit close retains `session::parent_` and the wrapper's `session_`,
preserving status and identity. Reconnect replaces the session; old completions retain the
old session and stop at their terminal-state guards. After owner destruction the retained parent address
may dangle, so terminal handlers must not dereference it. Timer finalization continues in
WS-06, preserving `conn_failed` for attempts that never connected and `disconnected` after
connection. Connection-stage guards accept `connecting` or `conn_failed`.

**Callback lifetime:** pending handlers retain their session's callback storage throughout
invocation; reconnect constructs independent storage in the replacement. `update_delegate()`
still replaces the current session's callable storage, so replacement during that same
invocation must be posted. Moves remain serialized outside active delegate invocation.
Complete status updates before notification, and recheck lifetime/state before accessing
the parent after user code returns. Broader reentrancy and exception policy remain follow-up work.

Do not finalize peer close by blindly calling `close()` from `on_control()`. That callback
runs inside a read operation, and Beast performs the protocol response and teardown.
Prefer recording the peer reason there and completing the terminal transition at read/close
completion. Keep the read needed by graceful shutdown alive. [Beast control-callback contract](https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/ref/boost__beast__websocket__stream/control_callback/overload1.html).

**Completion check:** cancel at each connection stage; fail each handshake; close repeatedly;
drain queued completions; disconnect with a ping wait outstanding; exercise callback-triggered
close and the selected reconnect policy. Verify one terminal event where promised, none after
explicit abort, preserved failure status, and no new work from an obsolete session. Extend the
existing loopback server only as needed for these cases.

<a id="ws-03"></a>

**WS-03. Contract and TODO cleanup**

**Source TODOs covered:**

- [`WS-PARAMS`](web_socket.cppm) — Review parameter contracts and validation.
- [`WS-PING`](web_socket.cppm) — Understand different types of pings and why we need enable_outbound_ping.
- [`WS-IDLE`](web_socket.cppm) — Review the idle timer and test it properly (perhaps with a local WebSocket server).
- [`WS-OP-TIMEOUT`](web_socket.cppm) — Review async operation timeouts; reproduce a case when a timeout is triggered.
- [`WS-LARGE-MESSAGES`](web_socket.cppm) — Test sending and receiving really big messages (e.g. a huge Coinbase initial snapshot); distinguish socket buffers from message limits.

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
- Describe callback lifetime per session: reconnect constructs independent callback storage,
  while `update_delegate()` replaces the current session's delegate. Preserve the restriction
  on replacing callable storage during its own invocation, and review reconnect reentrancy
  against the existing completion guards.
- Keep design questions marked as decisions; hiding Boost is not an established bug fix.

The repeated `expires_never()` in `on_handshake()` is a candidate for a separate small
simplification: the TCP deadline was already disabled in `on_ssl_handshake()`. Verify that
no path re-enables it before removing the call and its exception-reporting branch. This is
not just whitespace cleanup, because it removes a setup/error path.

**Completion check:** source and docs agree; every TODO maps to a batch below; existing
constraints remain visible. Use local links and symbol names so the report survives line shifts.

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

| Source TODO ID | Complete source TODO | Stages |
| --- | --- | --- |
| [`WS-PARAMS`](web_socket.cppm) | Review parameter contracts and validation. | [WS-03](#ws-03), [WS-04](#ws-04) |
| [`WS-PING`](web_socket.cppm) | Understand different types of pings and why we need enable_outbound_ping. | [WS-03](#ws-03), [WS-06](#ws-06) |
| [`WS-IDLE`](web_socket.cppm) | Review the idle timer and test it properly (perhaps with a local WebSocket server). | [WS-03](#ws-03), [WS-06](#ws-06) |
| [`WS-OP-TIMEOUT`](web_socket.cppm) | Review async operation timeouts; reproduce a case when a timeout is triggered. | [WS-03](#ws-03), [WS-06](#ws-06) |
| [`WS-LARGE-MESSAGES`](web_socket.cppm) | Test sending and receiving really big messages (e.g. a huge Coinbase initial snapshot); distinguish socket buffers from message limits. | [WS-03](#ws-03), [WS-05](#ws-05) |
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
`WS-DETACH` is retired in favor of retained parent links. Continue with the contract/TODO audit
in batch 3; timer cleanup and broader exception guarantees remain in batches 6 and 8.
Coordinate the timer/clock redesign in batch 6 with the shared execution proposal in
[current_work.md](current_work.md).
