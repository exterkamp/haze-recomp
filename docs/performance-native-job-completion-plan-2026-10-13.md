# Native job-completion wait replacement

Tracked in [#39](https://github.com/zdiemer/haze-recomp/issues/39), under #32.

The current carrier wait diagnostic records about 5.315 ms/frame on guest main
thread 1 in 330-us `sys_timer_usleep` calls at LR `0068EFF0`, with 2.581 ms/frame
overlapping the renderer's period before its first draw. The longest recorded
call is 16.676 ms. These are elapsed service scopes, not CPU utilization or
automatically removable time. See the shader-preparation report for the full
measurement method and limitations.

Source inspection identifies the prior caller path in `00682538`: check job
completion through `00682510`, delay through `0068EFC0`, service pending work
through `00722ED8` / `00723198`, then retry. This work servicing must continue
even while waiting. Completion includes the game work record and queue draining,
so removing the predicate or pretending a job completed would be incorrect.

The first experiment should replace only this completion-loop delay, protected
by exact game-code and call-site guards. Compare the original 330-us delay with
a bounded native spin/yield policy while retaining the original completion
predicate and pending-work service. Measure CPU consumption, ordinary present
distributions, loop iterations and any worker starvation; do not extrapolate
the entire sleep sum into an FPS gain. Prefer a native event after auditing all
completion producers and publication/wakeup ordering. Generic game delays and
audio timing are outside this replacement boundary.

The first guarded prototype is implemented; carrier comparisons are mixed and
the release default remains unchanged. Keep it on the experimental branch
and use focused commits. Required
gates include both camera test orders, the squad conversation, scene transitions,
shutdown and a long-run check for missed completion or lost work. Unlocked
gameplay timing and VSync behavior remain required; neither stable 144 FPS nor
consistent pacing is currently established.

## First prototype

`patch_job_wait.py` audits hashes of the completion loop, conditional delay
helper and float-duration syscall wrapper. It changes only two boundaries in
a build-local source copy, composing after the existing guest-pacer patch.
The completion predicate, queue/work servicing, and original duration
conversion and conditional call remain intact.

`haze_job_wait_call` establishes an exception-safe thread-local scope only for
guest thread 1, the audited TOC/call site and an in-range job index. Inside
that scope, the syscall replacement requires the original sleep LR, syscall
number 141 and exactly 330 us. Other calls take the original syscall path.
The native policy performs 32 processor pauses and yields to the scheduler
every 64 matching delays, then returns success. Every iteration still retries
the real completion predicate and services pending work. It writes no job
completion state, clocks or queue contents. This is bounded per-retry polling,
not an event-based wait and not a deadline guarantee under OS scheduling.

Runtime inspection clarified that the original Windows sub-millisecond
`sys_timer_usleep` already loops over `SwitchToThread` and QPC until its
requested deadline. The experiment removes that fixed deadline between
completion checks; it does not replace a coarse Windows Sleep call.

Internal `HAZE_NATIVE_JOB_WAIT=1` / `HAZE_JOB_WAIT_FILE` select the experiment.
The default stays off; no user-facing graphics/control setting is added.
Mode events and cumulative matching-delay/yield counts support comparison.
Guard tests prove the patch can be reversed to the exact original source,
composes with the pacer patch and rejects changed/repeated inputs. A native
fixture checks scoped handling, thread/TOC/call/job/duration rejection, syscall
result and exception cleanup. The full native regression gate passes after
fixing the unrelated Windows `near` macro collision in the camera test.

## Carrier comparison: no promotion

Root `076dd26`, SDK `6acf5d9`, ordinary candidate
`local/candidates/native-job-wait/HazeRecomp.exe`, SHA256
`59810358E489BFBC96FDF9574B845A13187CAA1EBD9137C64438751FB1260B7A`.
Run `20261013-004637-753`: 1440p, follow-resolution shadows, 1280x720 window,
AA off, AF 1, VSync on, uncapped; GPU occurrence mode 2, VP cache off
(CPU flags 5379), vertex/geometry caches and vertex workers off. Real audio
processing ran muted with private saves/input.

Each ordinary window has five seconds of warmup and 24 measured seconds,
covering six four-second yaw sweeps of +/-30 degrees. Both mode orders pass
the complete-present, actual-camera, mode-event and sampled-process checks.
Only the owned Haze was observed; other applications and activity between
process samples are not excluded. NPC/world state remains live.

| Order | Policy | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---|---:|---:|---:|---:|---:|
| First | Original | 668 | 35.895 | 48.574 | 56.459 | 73.495 |
| First | Native | 656 | 36.502 | 49.829 | 57.758 | 82.140 |
| First | Native | 670 | 35.730 | 48.368 | 54.067 | 61.380 |
| First | Original | 636 | 37.734 | 51.207 | 61.533 | 85.926 |
| Reverse | Native | 661 | 36.211 | 48.655 | 54.409 | 130.355 |
| Reverse | Original | 657 | 36.477 | 48.569 | 53.376 | 67.808 |
| Reverse | Original | 607 | 39.531 | 54.334 | 69.498 | 119.147 |
| Reverse | Native | 653 | 36.702 | 49.946 | 56.562 | 69.354 |

These results do not establish a consistent per-window improvement. The user
reported moving the window as a cause of one major dip, without an exact
benchmark timestamp. No interval has been deleted or attributed to that
movement without a matching timestamp. Maxima above retain all samples.

A separate, non-suspending thread-counter diagnostic follows the ordinary
windows: four seconds warmup, eight seconds measurement, original/native/
native/original. Host TID 14964 maps explicitly to guest main thread 1;
retained thread handles and creation time protect against TID reuse. Its
mode/camera/process/present validation also passes. Main-thread consumption
is 0.775/0.806/0.797/0.738 cores respectively, about 0.757 original versus
0.802 native on average. Associated mean intervals are 36.627/36.996/35.815/
36.810 ms. This short CPU diagnostic is separate from ordinary FPS evidence.
Process-wide CPU consumption in the ordinary windows spans 5.157-5.480 cores.

Mode-change counters indicate approximately two orders of magnitude more
completion checks under the native policy. They include warmup and setup,
so they are not exact per-frame counters. More frequent checking and slightly
greater main-thread CPU use have not demonstrated a sufficient pacing gain.

Both policies also run slower in absolute terms than the earlier GPU-occurrence
candidate. Cross-session results cannot identify the cause. Inspection confirms
the relocated generated PPU source still receives `/O2 /Ob1 /DNDEBUG` and the
generated-source flags; the current build has renderer profiling off and native
SPU arithmetic/FMA enabled. No lost source optimization flag was found. A
matched old/current binary comparison remains needed before attributing the
baseline difference to this patch, scene evolution or host conditions.

Private evidence: `local/native-job-wait-motion-{windows,report}.json`,
`local/native-job-wait-motion-presents.csv`, camera CSV and process JSONL;
`local/native-job-wait-thread-cpu.json` retains raw CPU counters, with separate
`-windows.json` and `-validation.json` derivatives. No game data is committed.

Keep the policy off by default and #39 open. Squad conversation, transitions,
long runs and completion-producer/publication auditing remain outstanding.
Do not treat the earlier 5.315 ms service scope as wholly removable time.
