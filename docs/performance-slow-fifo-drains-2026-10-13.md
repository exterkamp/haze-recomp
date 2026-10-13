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
