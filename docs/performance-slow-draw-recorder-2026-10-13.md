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

Next: ordinary three-versus-five-workload comparison first; then a controlled
jungle capture of this diagnostic and its disabled/enabled overhead. The
earlier 34 ms accumulated draw interval and multi-second ordinary stall remain
unexplained. Tracking #9 and #32 remain open.
