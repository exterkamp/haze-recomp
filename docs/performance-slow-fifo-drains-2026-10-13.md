# Slow FIFO drain attribution

Tracking #71, #9 and #32. The deferred-log capture's 463.142 ms pump interval
contains one 460.562 ms FIFO drain; a different 434.831 ms interval has 370
kick waits totaling 422.601 ms and 398 drains. The first requires separating
lock contention from the command walker/backend. The second needs upstream
command-production evidence; it is not one long event wait.

`GCM_SLOW_DRAIN_CSV` enables a bounded internal diagnostic around the existing
`cellGcm_rsx_process_fifo` wrapper. It records only calls over 2 ms, separating
QPC before FIFO-lock acquisition, lock acquired, and body complete. Each row
includes sequence, host TID, renderer-frame tag, guest GET/PUT snapshots,
context validity and optional thread-cycle delta. A fixed 16,384-event ring
retains recent events with explicit overwrite counts. Sequence increments
under the FIFO lock; ring insertion is separately synchronized, so consumers
must use timestamps/sequence rather than assume insertion order is execution
order across threads. Guest PUT snapshots are suppressed when context is not
initialized. The body interval includes the bounded state snapshots.

There is no hot-path logging or file I/O and no per-method timer. With tracing
enabled each drain has three QPC calls and two thread-cycle queries, plus
state reads; this is a diagnostic capture, not ordinary FPS acceptance. With
tracing disabled the wrapper takes the InitOnce fast path and original drain
flow. The CSV is written only by the explicit dump wired into Haze's normal
window-close path. It is not guaranteed on crashes, forced termination or
guest exit paths that bypass that close handler. Thread cycles are not wall
time and do not by themselves identify a lock owner or GPU wait.

Four focused native fixture cases compile the exact SDK diagnostic and wrapper
with a stand-in walker and real Win32 locks/timers. They verify disabled
guest/frame reads, enabled uninitialized-context read safety, held-lock versus
sleeping-body attribution, and a sentinel proving that a second dump does not
truncate or append. All pass. Ring-wrap/multithread ordering and game-specific
blocking attribution remain outside that fixture's coverage. The test is
included in `scripts/build.ps1 -Test`.

The ordinary profiling-off/FMA diagnostic candidate builds with two jobs:
`local/candidates/slow-fifo-drain/HazeRecomp.exe`, SHA256
`5DFBE41A2D5C5F41C57EC6230F180D3D6D930B69D06417423A1B738C2CF3441C`.
Logs: `local/slow-fifo-drain-build.log`, `local/slow-fifo-drain-test.log`.
This build also scopes the earlier Haze CPU-preparation startup call to its
Windows backend, matching that backend's declaration and allocation path.

## Live jungle capture

Run `20261013-135746-791`, prefix `local/jungle-slow-drain`, uses the same
copied jungle checkpoint, 1440p/8192 shadows, VSync off/uncapped, muted audio,
fixed CPU flags 13571, VP burst 1 and worker reuse 1. The scene is visually
verified after the tutorial. Tokens 2620/2621 select two 60-second absolute
camera sweeps after five-second warmups. No build, screenshot, external stack
sampling or heavy analysis overlaps those windows. Normal window close writes
the diagnostic CSV and one balanced renderer counter report.

The ring records 19,799 slow calls, retaining 16,384 and overwriting 3,415 early
records. Retained QPC coverage is 14119705225580..14121893641043 at 10 MHz;
this starts 87.135 seconds before the first gameplay window and ends 6.704
seconds after the last. Thus the discarded prefix precedes both windows.
All retained entries have unique sequences, ordered QPC boundaries, and host
TID 40692. There are 9,400 calls wholly inside gameplay windows. Their maximum
FIFO-lock wait is 0.0004 ms; maximum body time is 9.4899 ms (total 9.4901 ms).
This thresholded ring does not describe calls shorter than 2 ms or identify
which backend operation occupies body time.

QPC correlation against the pump CSV identifies the two frames over 50 ms:

| Pump frame | Total ms | Kick-wait ms / count | FIFO ms / drains | Present ms | Longest retained drain ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| 41662 | 172.981 | 164.207 / 142 | 6.376 / 152 | 2.113 | 4.375 |
| 33389 | 59.160 | 50.788 / 49 | 5.031 / 53 | 1.773 | 4.252 |

Both retained drains have effectively uncontended FIFO-lock acquisition
(0.0002 / 0.0001 ms). All seven pump frames above 33.333 ms predominantly
accumulate outer kick waits. The 173 ms interval reports queued work before
only two of its 142 waits; queued does not necessarily mean executable (a
semaphore can stop the walker). These observations point the next capture
toward upstream command production/job readiness. They do not reproduce or
explain the earlier 460 ms single-drain stall, and do not prove that FIFO-lock
contention can never occur.

For context only, the instrumented windows contain 9,953 present intervals,
12.055 ms mean / 82.95 FPS, p99 18.442 ms, maximum 172.980 ms, seven over
33.333 ms and two over 50 ms. Do not use this as an ordinary-build speedup.
Camera, native CPU flags and owned-process checks pass. Twenty-six resource
samples bracket timing; physical memory is 49.41-51.12%, no compilers observed,
maximum observer gap 5.359 seconds. Timing-buffer overhead has not been
measured by a separate on/off comparison.

Private evidence: `local/jungle-slow-drain-fifo.csv`, `-pump.csv`,
`-presents.csv`, `-windows.json`, `-report.json`, `-pooled.json`, resource and
process logs; correlated rows in `local/slow-fifo-capture-analysis.json`.
The next step is a timestamped producer/job capture around command-arrival
hitches. Remaining large drain-body stalls need backend/fence phase evidence
if they recur. Stable pacing and 144 FPS remain unresolved.
