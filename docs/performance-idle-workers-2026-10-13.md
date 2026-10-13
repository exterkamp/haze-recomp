# Reusing idle SPU job threads

Tracking: #36, #32, #9. Experimental branch; no stable-144-FPS claim.

The [per-frame trace analysis](performance-job-wait-frames-2026-10-13.md)
identifies worker startup as a strong slow-frame correlate. Its prior capture
does not exclude compiler contention and predates the job-0 arithmetic change.
An ordinary-build comparison is required before promoting this experiment.

## Replacement boundary

The SDK can reuse idle Windows host threads for exact, title-allowed workload
fingerprints. Haze allows full-image fingerprints for images 30, 41 and 43.
Text-only matches and all other jobs retain dedicated threads. Registration
is startup-only; re-registering an image clears its permission.

There is no pending work queue and no active-worker limit. Submission takes an
idle worker, starts a new retained worker if a slot is free, or creates a
one-shot overflow thread. Sixteen slots bound cached workers; idle threads
retire after five seconds. A waiting job cannot occupy the last worker needed
to start a dependency. Allocation/thread-creation failures leave job ownership
with the submitter, just as the previous path did.

The existing per-job callback still initializes byte-operation TLS, establishes
stack headroom, allocates and initializes context/local store, executes the
job, publishes taskset completion, releases context-owned MFC/coherency state,
and removes reservations referring to its host stack before returning. The
cache publishes a worker as idle only after this callback returns. Argument
snapshots still precede dispatch; local-store persistence remains its separate
existing policy. No guest task ordering, completion byte, or queue is replaced.

The execution driver resets its trampoline and saves/restores the thread-local
halt landing pad per invocation. Image 22 has a functional per-thread EXIT
counter and is excluded. Unknown/persistent controllers are also excluded.
Existing diagnostic PC history can persist on a reused thread; it does not
constitute architectural state. Cached stacks reserve 256 MiB each, matching
the existing thread path; this is virtual reservation, not committed RAM.

The cache has process lifetime. Busy jobs use the existing process-exit
behavior, with no new cancellation or promise to drain work at shutdown.
Idle workers expire without touching their slot after releasing its lock.

`SPU_WORKER_REUSE` and `SPU_WORKER_REUSE_FILE` are internal comparison controls,
not graphics settings. Profile builds distinguish reused dispatch intervals
from thread creation (SDK phase 9 / title trace kind 15); neither interval
is CPU time, and a worker can start before the dispatch call returns.

## Validation

The standalone Windows cache test passes 31 checks, including serial same-thread
reuse, eight simultaneous submitters transferring 800 stable arguments exactly
once, all-slots-busy overflow, dependent nested submission, startup failure
ownership, idle retirement and retirement/submission races. The header also
compiles as C. These tests prove cache behavior, not whole-game correctness.

The full ordinary build and regression suite passed with two build jobs. This
includes ABI fresh-register/argument/local-store and cleanup tests, taskset
dispatch/publication, MFC/coherency and existing renderer tests. Eleven trace
analysis tests and the present-mode evidence rejection tests passed separately.
Both live runs reached the copied jungle checkpoint, dismissed the Nectar
tutorial, and closed normally with exit code 0. First-reuse logs confirm all
three allowed images. All seven immutable baseline-save hashes are unchanged.

## Ordinary comparison

Measured executable SHA-256:
`78D47DC9B34B105DC0BC699BD5F03689E6C51128CD0B53D17E2B96DCAA92EF11`.
This has job-0 arithmetic enabled in both modes. The subsequent WIP default
selection adds `SPU_WORKER_REUSE=1` at title startup; the measured implementation
is otherwise unchanged.

Run `20261013-095340-326`, PID 15872. The first attempted comparison stopped
when its explicitly observed competing Haze process exited. Its incomplete
windows, process/pressure logs and measure log remain under
`local/jungle-idle-worker-reuse-overlap-*`. No result from it is used here.

The same owned game then completed four fresh 24-second windows in order
**0, 1, 1, 0**, with five-second warmups and absolute 30-degree yaw sweeps
(tokens 2070–2073). Graphics: 2560×1440, shadows 8192, FXAA, AF16, VSync off,
uncapped. GPU geometry mode 2, CPU flags 13571, VP consumer 1; producer and
completion-delay experiments off. Audio muted; disposable save copy; no
screenshots or builds during measurement. Gameplay/NPC state remains live.

| Mode | Frames | FPS | Mean ms | p95 ms | p99 ms | Maximum ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Dedicated threads, first window | 1738 | 72.45 | 13.802 | 18.016 | 22.319 | 38.059 |
| Idle reuse, first window | 1907 | 79.50 | 12.579 | 16.129 | 19.559 | 28.288 |
| Idle reuse, second window | 1913 | 79.79 | 12.533 | 16.254 | 19.403 | 62.910 |
| Dedicated threads, last window | 1805 | 75.23 | 13.293 | 16.800 | 20.709 | 32.082 |
| Dedicated threads, pooled | 3543 | **73.84** | **13.543** | **17.464** | **21.678** | **38.059** |
| Idle reuse, pooled | 3820 | **79.64** | **12.556** | **16.172** | **19.538** | **62.910** |

Pooling uses raw complete present intervals, not averaged percentiles. Mean
frame time decreases 0.987 ms (7.29%); throughput increases 7.86%. p95 decreases
1.292 ms and p99 2.139 ms. **Rare hitches are not fixed:** reuse has two frames
over 33.333 ms, including one at 62.910 ms, against one over 33.333 ms on the
dedicated path. No outlier was removed. The result is not a measurement of
startup CPU savings and does not explain that longest hitch.

All four full windows pass native mode, present continuity, camera and Haze
process audits. The pressure observer brackets the windows with 22 samples,
maximum gap 5.341 seconds, memory 47.18–49.57%, and zero observed compiler
processes. Only the owned Haze process was observed. This cannot exclude other
applications or process lifetimes between samples.

Private prefix `local/jungle-idle-worker-reuse`: `-windows.json`, `-report.json`,
`-pooled.json`, `-presents.csv`, `-camera.txt.csv`, `-processes.jsonl`,
`-pressure.jsonl`, `-measure.log`. Build log: `local/idle-worker-reuse-build.log`.

## Independent repeat and remaining acceptance

A fresh copy/run `20261013-100125-640`, PID 52588, completed reversed order
**1, 0, 0, 1**, tokens 2080–2083. Its isolation audit **fails**: another Haze
PID 40192 starts during the second window and remains thereafter. The first
observation is QPC 13979194554713. This run cannot establish an independent
isolated gain; preserve all windows, including its 594.551 ms and 83.287 ms
maximum intervals. The explicit `-contended-report.json` retains mode/camera
results and failure annotation. Evidence prefix: `local/jungle-idle-worker-repeat`.

The experimental Haze build selects reuse automatically; users need no extra
option. Explicit zero/internal file controls remain for diagnosis. This is a
limited promotion based on the complete isolated ABBA run, not completion of
#36 or the frame-pacing goal. Independent isolated repetition, direct startup
phase profiling, the 62.910 ms hitch, deck/conversation, transitions and broader
gameplay remain open. The existing VSync and frame-cap options are unchanged.

SDK implementation: `4bef1a56c419bb89fc507445882f4ee403d4d885`.
The incremental ordinary WIP build with automatic selection passed; its SHA-256
is `CB9052BB4848C5628C24571BCB2E9AC1AEF748B5A455B7BD8F328B81FE7028E4`.
The WIP launcher is staged at
`local/releases/haze-wip-2026-10-13-idle-workers/Play-Haze.cmd`, preserving the
previous WIP and its current graphics settings. Interactive play uses audio
and normal saves; all tests above used muted disposable saves.

## Isolated confirmation contradicts an unconditional gain

The fresh ordinary WIP run `20261013-101551-895` (PID 38632, exited normally)
completed reversed order **1, 0, 0, 1**, tokens 2100–2103, with the same
24-second windows, five-second warmups, checkpoint and settings. All full-window
mode, camera, present and process audits pass. The pressure observer recorded
22 samples, maximum gap 5.859 seconds, memory 46.74–47.40%, and no compiler or
other Haze process. This still cannot exclude unsampled background activity.

| Reuse | Intervals | Mean ms | FPS | p95 ms | p99 ms | Maximum ms |
|---|---:|---:|---:|---:|---:|---:|
| 1 | 1061 | 22.091 | 45.267 | 23.300 | 107.499 | 3136.075 |
| 0 | 1574 | 15.239 | 65.623 | 19.342 | 27.142 | 47.769 |
| 0 | 1582 | 15.161 | 65.958 | 19.745 | 28.215 | 52.354 |
| 1 | 1891 | 12.680 | 78.866 | 16.347 | 19.539 | 32.568 |

Pooling complete intervals gives dedicated **15.200 ms / 65.790 FPS** versus
reuse **16.062 ms / 62.258 FPS**. Reuse has 30 intervals above 33.333 ms and
18 above 50 ms; dedicated has 20 and one respectively. No outlier was removed.
The final reuse window is faster, but the complete run does **not** reproduce
the previous pooled gain. Neither a general improvement nor a causal reuse
regression is established by these order-dependent results.

The 3136.075 ms present gap (QPC 13987713667731–13987745028481) overlaps pump
frame 28781: total 3307.044 ms, FIFO-drain scope 3129.998 ms, present scope
173.399 ms, idle wait 0.0016 ms, reported thread CPU 15.625 ms. Other large
stalls fall in present, FIFO and idle-wait scopes. These coarse wall-time
buckets do not identify a lock, GPU fence, scheduler delay or paging cause.
Both modes improve later, making checkpoint settling a lead requiring a
controlled capture, not a reason to discard the first window.

Evidence prefix: `local/jungle-idle-worker-confirm` (`-windows.json`,
`-report.json`, `-pooled.json`, `-pump.csv`, `-presents.csv`, `-camera.txt.csv`,
`-processes.jsonl`, `-pressure.jsonl`). The current WIP hash is the CB9052BB
hash above. Consistent pacing and independent performance acceptance remain
open. Windows WPR CPU capture failed to enable the system profiling policy
(0xc5585011); no ETW capture was obtained. The next diagnostic uses the existing
in-game recorder with a larger bounded history and checks actual retention.
