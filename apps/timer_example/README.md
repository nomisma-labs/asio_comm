# Timer example

A small Assembly/relay application: arm `nom::timer`, relay each expiry to an output node,
and rearm until the requested number of ticks has been delivered. Configuration and validation
are in [config.h](config.h). The I/O node and service commands come from
[common_app_nodes.h](../common_app_nodes.h); completion state belongs to the timer source.

```sh
cmake --preset dev
cmake --build --preset dev --target timer_example
build/dev/bin/timer_example
build/dev/bin/timer_example 250 5
```

Defaults: three ticks, 100 ms apart. Arguments are delay in milliseconds (1..60000) and tick
count (1..100). Each successful expiry prints `Tick`.

Rearming from the callback implements fixed-delay scheduling; callback execution and scheduling
latency add to the interval. The stop command calls `cancel()` without emitting another tick.
Timer expiry is asynchronous, and relay routing itself remains synchronous. The context outlives
the timer, all callbacks run on its single thread, and the process drains canceled handlers.
See [the timer contract](../../docs/asio_comm/timer.md) for generation-based cancellation,
fatal error policy and ownership. This example is included in all normal build presets, including `dev`.
