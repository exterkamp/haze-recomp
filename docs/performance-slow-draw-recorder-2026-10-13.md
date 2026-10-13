# Bounded draw-stage and thread-cycle diagnostic

The prior jungle trace recorded 34.349 ms in the renderer's draw-callback
scope across 435 draws. That was accumulated wall time, not one draw and not
measured CPU execution. Existing nested vertex/pipeline/texture timers did
not account for most of it. SDK commit
`5108fbe07f22424f060b27067f6d51c0347c18fe` adds a bounded diagnostic to identify
the remaining regions without writing a log for every draw.

Only `YZ_PERF_PROFILE` builds compile the code and storage. Setting
`RSX_SLOW_DRAW_CSV` enables it; `RSX_SLOW_DRAW_THRESHOLD_US` defaults to 250.
Ordinary builds contain neither the instrumentation nor its path environment
string. This is a diagnostic control, not a release graphics option.

Each callback records QPC boundaries, host thread/frame/callback IDs, basic
draw counts and target/texture metadata. Four exclusive regions partition its
wall time: geometry preparation, pipeline/constants, resource resolution,
command recording. These name regions of the code, not mutually exclusive
API categories: for example, resource resolution can also record barriers,
and flushes can occur inside an earlier region. Existing nested timers must
not be added on top of these regions. The wrapper finalizes early returns.

Two `QueryThreadCycleTime` calls record raw scheduled-thread cycle counts,
with validity and monotonicity checks. They are explicitly **not CPU
milliseconds** and must not be converted using QPC frequency. Comparing wall
time and cycles can help distinguish blocking/descheduling from active work,
but does not identify a particular wait or prove a CPU bottleneck alone.

The recorder retains the last 256 callbacks exceeding the threshold and dumps
on normal close/shutdown. Counters report total callbacks and overwritten
qualifying events. It is a filtered suffix, not complete-frame coverage;
absence from the ring cannot establish absence of a stall. Instrumented
overhead and capture retention must be assessed before using it for a
performance conclusion. No rendering operations were changed.

## Validation

- Profile candidate compiled successfully:
  `local/candidates/idle-worker-five-slowdraw/HazeRecomp.exe`, SHA-256
  `33A985770A362B0C83EDCEFD32C3D258F29148FC172B94DA28CA1D54E7CFFB3A`.
- Ordinary five-workload candidate compiled and passed the full existing
  regression suite using two compiler jobs:
  `local/candidates/idle-worker-five-ordinary/HazeRecomp.exe`, SHA-256
  `A6ACBC36AE4C5D9022D2750C8A86DCB783754C3A4A4F373331D1CB0FC7D883A7`.
  Binary inspection found `RSX_SLOW_DRAW_CSV` in the profile candidate and
  absent from the ordinary one, consistent with the compile-time guards.
- Muted disposable-save startup smoke run `20261013-105839-044`, PID 25352,
  exited normally. Threshold deliberately set to **1 microsecond** to exercise
  rollover; this was a recorder smoke test, not gameplay/FPS validation.
- Dump counters: 737465 callbacks, 737209 overwritten, 256 retained. CSV
  callback IDs 737210–737465 are monotonic; retained frame IDs 21544–21553.
  All 256 cycle queries are valid, with nonnegative deltas matching the raw
  counters. Every row is rectangular; QPC frequency and wall-time conversion
  validate; four stage times sum to total wall time within output precision.

Private evidence: `local/slowdraw-smoke.csv`,
`local/runs/20261013-105839-044/boot.stderr.log`,
`local/idle-worker-five-slowdraw-build.log`, and
`local/idle-worker-five-ordinary-build.log`.

## Isolated jungle capture

Run `20261013-110406-618` used the profile candidate above, fresh disposable
jungle save, 1440p/8192 shadows, VSync off and no cap. After dismissing the
tutorial and activating Nectar, two 24-second absolute camera sweeps used
five-second warmups, worker reuse enabled throughout, tokens 2160/2161.
Audio was muted and the process exited normally. No job-phase recorder ran.
Camera, mode, present and Haze-process audits pass. The resource observer
brackets both windows: 12 samples, 47.80–48.28% memory, zero observed compilers,
maximum gap 5.366 seconds. Other applications and activity between samples
are not excluded by these observations.

Across 3866 retained present intervals: mean 12.409 ms / 80.588 FPS,
p95 15.673 ms, p99 18.095 ms, max 26.309 ms, zero gaps above 33.333 ms.
These are instrumented results, not an ordinary-build gain or a measurement
of diagnostic overhead. The prior large stalls did not recur in this capture.

At a 1000-microsecond threshold, the recorder retained 256 of 329 qualifying
callbacks from 9,017,227 total callbacks. Its first retained QPC predates both
measurement windows. Ten callbacks are fully inside the first window and four
inside the second, with no boundary-crossing callbacks. The new
`scripts/analyze_slow_draws.py` validates CSV shape, counters, clock conversion,
stage conservation and raw cycle deltas before reporting retained events.

The largest callback is 2.6515 ms, of which 2.6164 ms is resource resolution.
Its enclosing renderer frame records one `CreateCommittedResource` taking
2.5238 ms within 2.5940 ms of texture decoding. Another callback takes
2.3332 ms, including 2.3069 ms resource resolution; its enclosing frame records
one 2.2429 ms resource creation within 2.3015 ms texture decoding. These scopes
overlap and must not be summed. Their temporal alignment identifies resource
creation as a concrete lead; it does not explain all frame time or earlier
large stalls.

Several 1–1.5 ms geometry/pipeline callbacks have relatively low raw thread
cycle deltas (for example, 1.4378 ms geometry and 126,900 cycles). This supports
investigating waits or descheduling inside those regions. Flush/fence timers
also occur in enclosing frames, but the recorder does not yet associate a
specific flush reason with an individual callback. No causal assignment is
established. Join by QPC: slow-draw labels are renderer frame minus one and
pump frame plus two in this capture, not identical frame IDs.

Private evidence prefix: `local/jungle-slowdraw-stages`, including validated
draw analysis, present report, raw CSVs and process/resource observations.
Diagnostic overhead, per-draw flush attribution and the earlier multi-second
stall remain open. Tracking #9 and #32 remain open.
