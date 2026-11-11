# Timer

`nom::timer` is an Asio-backed, one-shot monotonic timer exported by `nom.asio_comm` from its
`:timer` partition. Include Boost's executor/context headers at the composition boundary.
See the [example](../../apps/timer_example/README.md).

```cpp
nom::init_logger(nom::main_logger); // Once at application startup; import nom.core.
nom::timer deadline(io.get_executor());
deadline.arm_after(std::chrono::seconds{2}, [] {
    // Handle successful expiry.
});
```

| Operation | Contract |
| --- | --- |
| `arm_at(due, callback)` | Replace the active wait and return its deadline. Accept past/current deadlines; callback delivery is always asynchronous. Reject an empty callback. |
| `arm_after(delay, callback)` | Compute and return a deadline from `steady_clock::now()`. Zero delay is valid; reject negative/overflowing durations and empty callbacks. |
| `cancel()` | Invalidate the current wait without delivering a cancellation event. Return whether a generation was armed, even if its successful completion was already queued. Idempotent and nonthrowing. |
| `is_armed()` | True until the current wait completes or is canceled. False before entering its callback. |
| Destruction | Invalidate the wait without calling user code. Internal completions may drain afterwards. |
| Move construction | Transfer the wait and callback without cancellation; nonthrowing. |
| Move assignment | Cancel the destination's existing wait, then transfer the source's wait and callback; nonthrowing. Self-move does nothing. |

A moved-from timer is empty: `is_armed()` and `cancel()` return `false`, destruction is safe,
and it can be reassigned. Arming it with valid arguments throws `std::logic_error` until reassignment.
Moving retains the original executor and callback captures; it does not redirect captured owner references.

Cancel/rearm suppresses obsolete callbacks even when successful expiry was already queued,
including dispatch delayed beyond a replacement deadline. Generation checks provide this
guarantee; [backend cancellation alone does not](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference/basic_waitable_timer/cancel.html).

The parameterless callback reports successful expiry. Ordinary cancellation is suppressed.
An unexpected Boost timer error terminates the application with a fatal error message.
Initialize `main_logger` before timer use, including HTTPS deadlines, and respect the logger's
calling-thread requirements.

Delivery may be late; callers can retain the returned deadline and read the clock if timing is needed.
The timer is disarmed before invoking the callback, so the callback can rearm, cancel, or destroy it.
For repeated ticks, rearm explicitly and choose fixed-delay or fixed-rate scheduling in the caller.

Different timer instances can run on different threads. For each instance, construction,
operations, destruction and callback delivery must be serialized on the same execution domain;
use one `io_context` thread or an externally managed strand. The context must outlive the timer
and its pending handlers. Moves must be serialized with operations and callback delivery of
both involved timers. Copying is disabled. Cancellation does not interrupt a callback already running.

Invalid arguments leave an existing wait unchanged. Allocation/backend initiation failure after
replacement leaves the timer disarmed and throws. Callback exceptions propagate through the
executor's `run()` without being reported as timer errors. Cancellation/destruction suppress
backend cancellation exceptions after logical invalidation.

The current timer and example are **complete**. HTTPS deadlines and WebSocket connection and
active/passive Ping waits use this timer. Backend-neutral execution, virtual time and replacing
the emergency logging block with the planned `FATAL` helper are
[separate follow-up work](../../src/asio_comm/current_work.md#https-timer-completion).

Validation covers deferred expiry, queued-success cancellation/rearm after a delayed dispatch,
capture release, moves and moved-from reuse, callback reentry/destruction, independent timers
on separate threads, invalid arguments and callback exceptions. Fault injection verified fatal
logging and termination.
