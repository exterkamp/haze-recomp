# Completion loops and a captured long renderer frame

Implementation: root `bddc61c`, SDK `9f44d36`. This is diagnostic evidence for
#39 and #9, not a new performance improvement or a replacement WIP release.

## Build and run

`scripts/build.ps1 -SpuFma -ProfileRenderer -Test -OutputDirectory
local/candidates/job-critical` passed. The exact executable SHA256 is
`65545216F7922D2779F3A69AB059721052982A3E46284FBF4654A8E78E263C23`.
Full regression log: `local/job-critical-build.log`. The profiler's guarded
source patch composes with the existing pacer patch without changing original
calls; runtime tests cover guards, phase conservation, nested loops and ring
wrap. Four summary tests cover boundaries, overlap, invalid counts and coverage.
Actual Ninja rules confirm the diagnostic compile flag on all three relevant
translation units. Ordinary builds compile the completion scopes to no-ops.

Run `20261013-042214-964`, PID 56596, used a private save/input fixture, real
audio processing muted, 2560x1440 rendering, follow-render-resolution shadows,
1280x720 client window, VSync on, cap zero, GPU occurrence mode 2, native index
preparation and VP consumer on; VP producer and shortened completion delay off.
Vertex workers and geometry/vertex caches were off. Renderer profiling used
`RSX_PROFILE_LIGHT=1`; completion and pump CSVs were enabled. No sampler,
build or screenshot ran inside the measurement windows. A preceding capture
verified the carrier deck. The owned process closed normally with exit zero;
post-close CIM found no Haze or Labyrinthine process.

Both 24-second windows used a repeating absolute +/-30-degree camera sweep
with a four-second period after five seconds of warmup. Present/mode/camera/
process audits passed. Only the owned Haze was observed. Host CPU activity was
53.3% and 69.6%; other background applications were not excluded. Sampled window
rectangles stayed constant, which cannot rule out activity between samples.

## Completion loop measurements

The profiler records complete main-thread invocations of `00682538`, their
job handle and return address, elapsed phases and call counts. It retains
65,536 rows in memory and writes on normal close. This run completed 66,102
loops; retained records bracket both windows. Nested loops are not added to
their parents. The second window excludes one crossing loop with 0.4191 ms
of overlap; the first has none. Loops still active at shutdown are excluded.

| Measurement | Sweep 1 | Sweep 2 |
|---|---:|---:|
| Presents | 770 | 732 |
| Complete top-level completion loops | 13,594 | 13,221 |
| Loop elapsed total | 4625.893 ms | 4936.741 ms |
| Completion predicate, including its queue processing | 146.077 ms | 171.505 ms |
| Existing delay | 4473.327 ms | 4758.031 ms |
| Pending flag check | 2.870 ms | 3.688 ms |
| Pending service branch calls | 0 | 0 |

The delay accounts for 96.7%/96.4% of this loop's elapsed time. Normalized to
the windows' presents, predicate processing is only 0.190/0.234 ms per frame,
versus 5.810/6.500 ms for the delay. These are elapsed scopes, not removable
latency estimates. Previous faster-poll experiments had mixed frame pacing.

Three return addresses account for almost all loop time: `007E5414`
(1698.334/1899.590 ms), `001F6930` (1424.822/1447.341 ms), and `00697570`
(1497.841/1584.156 ms). These give specific producer/caller targets for the
next dependency audit. The longest individual loop was 20.797 ms in sweep 2,
including 20.463 ms of delay across 29 retries.

Source audit identifies a 512-slot table with 0x1450-byte slots. Byte +8 is
job state; byte +9 selects a path that drains the SPURS result queue before
rechecking state. State publication uses `006823C8` / `00682100`, with reset
and slot reuse also writing state. Blocking only on a slot flag could strand
completion processing performed by the waiting thread. A native wait must
cover queue publication, status changes and reset/reuse without losing wakeups.

The helper called at `00C27B0C` resolves to `sys_ppu_thread_get_id`, registered
by computed NID in `ppu_sysprx.cpp`. An initial numeric-only search missed it;
it is not an unresolved-import blocker. The separate pending branch includes
`sys_timer_sleep(1)` but was never entered in these measured windows. This is
not evidence that it causes the previously reported conversation slowdown.

## Frame pacing and the long tail

These are profiled results and must not be used as an ordinary-build A/B gain.

| Present interval | Sweep 1 | Sweep 2 |
|---|---:|---:|
| FPS | 32.13 | 30.55 |
| Mean | 31.125 ms | 32.737 ms |
| p95 | 41.684 ms | 44.073 ms |
| p99 | 47.805 ms | 53.195 ms |
| Maximum | 169.233 ms | 67.313 ms |

Pump partition means: event wait 13.644/15.286 ms, FIFO 16.006/15.964 ms,
present 1.291/1.270 ms, tick plus other 0.183/0.218 ms. Renderer draw scope
12.828/12.725 ms and GPU 2.219/2.172 ms are nested/overlapping measurements;
do not add them to the pump partition.

The worst pump interval, frame 14548, spans QPC
`13775059133935..13775060832848` and lasts 169.891 ms. Its partition is
12.969 ms wait, 155.058 ms FIFO, 1.820 ms present, and 0.044 ms other/tick.
The nested draw wall scope is 152.410 ms, but vertex packing is 1.281 ms,
fence wait 2.442 ms, VP/FP preparation 1.194/1.258 ms, texture decoding
1.072 ms, PSO lookup 0.097 ms, and GPU execution 2.092 ms. Completion-loop
overlap with this interval is only 17.234 ms. Renderer-thread CPU accounting
is 15.625 ms; Windows accounting is coarse, but does not support describing
the entire 152 ms draw scope as CPU computation.

Pump and Present samples use different boundaries, explaining their slightly
different maximum intervals. No outlier was removed. The long draw interval
suggests an unmeasured blocking/preemption component; it does not identify
the responsible driver call, mutex, scheduler activity or application.
Next: locate that elapsed-time gap using submission subscopes and/or an
OS scheduling trace, alongside producer readiness for the three job callers.
Rewriting the relatively cheap completion predicate is not the priority.

Private evidence: `local/job-critical-{summary,pump-summary,present-report}.json`,
`local/job-critical-{loops,pump,renderer,presents}.csv`, windows manifest,
camera CSV and process JSONL. No private game content is committed.
