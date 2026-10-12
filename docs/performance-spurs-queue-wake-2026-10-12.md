# SPURS queue completion profiling and native wake experiment

The earlier squad conversation capture reproduced 1–2 FPS. Its main thread
waited on condition 7, signalled by guest worker 37 after SPURS queue work.
See [the dependency trace](squad-slowdown-profile.md). This experiment follows
that measured dependency rather than changing the renderer or assuming a
different graphics API fixes the stall.

## Queue cost diagnostic

Muted run `local/runs/20261012-064147-539` used a profiling build, 2560×1440
rendering, `shadow_resolution=follow`, a 1280×720 window and a 30 FPS cap.
It reached the actual squad conversation and completed it. Another agent's
Haze instance was running, and completed-job tracing was enabled; this is
dependency evidence, not a controlled FPS comparison. It did not reproduce
the sustained 1–2 FPS interval of the earlier capture. The owned process was
closed normally with exit 0 at 06:50:04 UTC.

For frames 6300–7400, four worker result queues in taskset `014E1600` account
for about 44.4 seconds of scoped wall time during an 84.15-second interval:

| Queue | Calls ≥200 μs | Total ms | Lock acquisition ms | Polling sleep ms |
| --- | ---: | ---: | ---: | ---: |
| `014E6900` | 6,115 | 16,480.21 | 635.38 | 15,837.00 |
| `014E3980` | 5,930 | 14,191.67 | 135.96 | 14,049.15 |
| `014E3000` | 5,198 | 10,712.90 | 190.16 | 10,517.67 |
| `014E4300` | 1,007 | 3,010.84 | 60.09 | 2,948.66 |

About 97.7% of those scopes is polling sleep, 2.3% is lock acquisition, and
the remainder is small. There were 26,299 empty observations and 26 claimed
header observations. Sleep averaged roughly 1.65 ms per unavailable/claimed
observation. These numbers establish where the waiter spends time; they do
**not** establish when each producer became ready or how much faster wakeups
can save. Completed-job timing also records native execution, setup and
thread creation separately; persistent jobs that do not return are absent.

Local evidence: `squad-queue-conversation.json`,
`squad-queue-conversation-spu.json`, `squad-queue-costs/`, and
`squad-queue-waits/`. These remain ignored because captures contain game data.

Profiling builds alone compile queue probes. `SPURS_QUEUE_PROFILE` selects an
existing output directory; `SPURS_QUEUE_PROFILE_CONTROL` optionally arms the
recorder through a 0/1 file, checked per thread once per second. The per-thread
CSV partitions each slow scope into lock acquisition, blocking wait, and
remaining wall time. Formatting is outside the scope but still adds overhead.
`scripts/summarize_spurs_queues.py` excludes boundary-crossing scopes from cost
totals and reports their count. Never add these queue scopes to their enclosing
PPU HLE scopes as if they were independent time.

## Opt-in native queue publication wake

The candidate uses Windows `WaitOnAddress` on a queue's raw eight-byte header.
Registration and the snapshot occur under the existing SPU atomic-line lock.
Coherent SPU/PPU writers call `WakeByAddressAll` when that header actually
changes. Publication between unlocking and waiting returns immediately because
the comparison differs. Every return rechecks the real queue predicate under
the lock; it does not fabricate a result, skip a task or change guest time.
These semantics follow the [Windows wait API](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitonaddress).

Watches are reference counted and removed before the HLE call returns, so
queue deallocation leaves no retained host pointer. The bounded hashed watch
cache may decline a colliding registration. A one-millisecond timeout retains
polling for that case and for writers without a coherence notification.

`SPURS_QUEUE_WAIT_NATIVE=1` enables the experiment. It remains **off by default**.
`SPURS_QUEUE_WAIT_CONTROL` selects a live 0/1 diagnostic file, checked at most
once per 100 ms by each calling thread. Native mode transitions log their
actual host TID and QPC timestamp. Ordinary builds compile out the timing
probes but retain this opt-in experiment.

Correctness checks cover real ordered delivery of 512 results across ring
wraps, both coherent notification paths, overlapping watches, protected freed
queue mappings, claimed and empty queues, and a producer with no notification.
The timing oracle separately distinguishes empty, claimed and lock-contention
delays. The benchmark reporter rejects missing/wrong-thread mode evidence,
late transitions, transitions within measured windows, missing presents and
insufficient intervals.

## Ordinary-build A/B

The candidate is `local/candidates/native-queue-wait/`;
timed windows use `local/native-queue-abba-windows.json`. The test alternates
polling/native/native/polling for 60 seconds each after five-second warmups,
without camera motion, readbacks, CPU sampling or builds during those windows.
It retains every successful-present interval and every stall. The selected
critical host threads must have settled mode evidence before each window.
An independent two-second process log records other Haze instances.

Run `local/runs/20261012-065837-945` used ordinary profiling=0, SPU FMA=1,
comparison=0. SHA-256 of its executable is
`E9759A64933341316FA1A96C7FC9BBDDD724BA19B50721C0AC8D5CD5D3C672FF`.
The camera stayed at the opening carrier position, visually checked before
and after measurement. No other Haze process appeared in 196 two-second
samples from 07:02:14 through 07:08:49 UTC. The owned game exited normally
with code 0 at 07:08:09. Native mode timestamps on host threads 21360 and
53000 settled before every measured window; the strict reporter passed.

| Mode, in order | Frames | FPS | Mean ms | p95 ms | p99 ms | Max ms | Mean CPU cores |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Polling | 718 | 11.99 | 83.43 | 124.73 | 157.63 | 219.29 | 3.63 |
| Native | 843 | 14.07 | 71.06 | 106.96 | 158.47 | 419.66 | 4.00 |
| Native | 937 | 15.63 | 63.97 | 98.56 | 157.10 | 369.15 | 4.47 |
| Polling | 839 | 14.00 | 71.42 | 106.17 | 177.87 | 419.53 | 4.23 |

Pooling the two windows of each mode gives 12.99 FPS polling and 14.85 FPS
native, about 14.3% higher in this run. CPU use per presented frame was
303.38/302.22 ms polling and 284.90/286.14 ms native. Native mode's higher
CPU use per second accompanies more frames, so it is not a CPU-cost saving
per second. Large frame spikes remain. Scene activity and the difference
between the two polling windows limit the causal estimate; this is not yet a
repeatable 30 FPS result or a revision-to-revision comparison with an older WIP.
The experiment remains off by default pending independent reversed-order
validation. The full native regression gate passed in
`local/native-queue-wait-tests.log`, including the new queue/coherency oracles.

A fresh reverse-order attempt, `local/runs/20261012-071236-939`, verified
startup and menus with native waits enabled, but scripted held menu input
repeated a selection and entered chapter selection. It produced no measured
gameplay windows and is excluded from the comparison. The owned process exited
normally with code 0 at 07:21:07 UTC. No chapter overwrite was confirmed.
Reverse-order gameplay validation remains outstanding. Short menu pulses and
visual checks at each transition are required for the next attempt; poll
counts must not be treated as elapsed time. The final profiling build also
passed its actual compile-rule guard for all six PPU/SPU/queue/RSX sources in
`local/squad-native-queue-final-build.log`.
