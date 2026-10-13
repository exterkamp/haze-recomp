# FIFO wait outcomes and inner drain phases

Tracking: #71, #35 and #32. This is diagnostic attribution and a scheduling
experiment; the normal one-millisecond timeout is unchanged.

The isolated shader-bundle training run averaged 11.035 ms/frame, with 5.539 ms
in outer pump waits, 3.612 ms in FIFO drains and 1.818 ms in presentation.
The wait total consists of repeated calls, and does not establish that its
whole duration is removable. Previous captures did not record whether each
call returned because it was signaled or timed out. An older zero-timeout run
used a different unvalidated scene, so it is not current performance evidence.

## Measurement changes

`cellGcm_fifo_kick_wait_result` exposes the actual wait outcome: signal,
timeout, fallback Sleep, or failure/other. The old void API remains a wrapper.
The pump diagnostic records per-frame counts and elapsed time for each outcome,
maximum individual wait, and queue snapshots before/after waiting. Outcome times
partition the original wait scope; they include scheduling delay and the
pre-wait queue query. Queued work may still be blocked on a semaphore. An empty
FIFO after a signal does not identify why the signal occurred.

The existing bounded slow-drain ring now partitions its body into setup,
walker, postwalk, recycle, post-recycle diagnostic bookkeeping, drained-EA
publication, guest GET write, reference-lock acquisition and reference
publication. A residual accounts for wrapper snapshots and boundaries. Stopped
reason and GET/PUT snapshots identify empty or blocked drains. Clocks and phases
are active only with the existing slow-drain diagnostic; output remains deferred
until normal close. Nested phases must not be added to outer body time.

The existing timeout file control is recorded by timestamped startup/change
events. The `fifo_idle_wait` benchmark mode requires matching evidence across
each full camera window. A 1/0/0/1 sequence will test one-millisecond event waits
against polling with a zero timeout in one process. The zero-timeout mode may
consume additional CPU; it is not a proposed release default without evidence.

## Verification

- Actual pump accounting headers pass partition/reset/ring-wrap tests, including
  signal/timeout/Sleep/failure categories, empty signals, and reporting clocks
  only on diagnostic mode changes. Execution exits 0; logs
  `local/pump-wait-outcomes-{tests,execution}.log`. The initial PowerShell wrapper
  reports an error for redirected native stderr despite the fixture printing
  PASS; explicit subprocess capture verifies executable exit 0.
- `tests/test_pump_summary.py`: 16 passing tests, retaining legacy CSV support
  and rejecting partial or inconsistent outcome schemas.
- `tests/test_present_modes.py`: four passing tests including full-window idle
  timeout evidence and rejection of missing or mid-window changes.
- Inner FIFO fixture: 15 passing cases, including simulated long lock,
  publication/GET-write/recycle/setup scopes, disabled clocks, and early return.
  Analyzer: 11 passing tests. Commands and SDK source hash are in
  `local/slow-drain-inner-validation.txt`.
- Two-job ordinary incremental build exits 0. Candidate
  `local/candidates/fifo-wait-phases/HazeRecomp.exe`, 122,840,576 bytes,
  SHA-256 `6A1093A69AE58CB22F9BED4205275C9DFC82FC0EEE904D3206A8A1921AEA260C`.
  Build log: `local/fifo-wait-phases-build.log`.

## Detailed-renderer comparison and hitch attribution

Run `20261013-225551-609`, PID 24972, uses that same candidate with detailed
FIFO/draw/flush diagnostics enabled. Four 20-second windows alternate timeout
1/0/0/1, with five-second settling periods and absolute camera sweeps at the
copied jungle checkpoint. Graphics are 1440p, 8192 shadows, VSync off, uncapped;
audio is muted. Every recorded interval, including the large hitch, is retained.

| Timeout ms | FPS | Mean ms | p99 ms | Max ms | Wait ms/frame | FIFO ms/frame | Drains/frame |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 83.942 | 11.913 | 18.043 | 39.983 | 6.057 | 3.896 | 9.91 |
| 0 | 82.872 | 12.067 | 18.232 | 181.626 | 0.127 | 9.954 | 550.30 |
| 0 | 74.732 | 13.381 | 21.317 | 27.860 | 0.147 | 11.143 | 579.24 |
| 1 | 78.933 | 12.669 | 19.450 | 31.634 | 6.584 | 4.138 | 10.50 |

Mode and camera validation pass. Strict process-isolation checks fail because
another Haze instance, PID 48684, overlaps. Resource sampling has a 5.441-second
maximum gap, 52.34-54.00% physical memory use, and no observed compiler. No owned
compilation or screenshots occurred in the timing windows. This comparison
does not establish a causal performance gain or regression.

Zero timeout replaces waiting with hundreds of FIFO calls per frame. Pump
thread CPU rises from about 3.96/4.31 ms at timeout 1 to 10.03/11.08 ms at timeout
0. Detailed drain diagnostics themselves perform thread-cycle queries and
phase clocks on every call, so they can disproportionately penalize polling.
A repeat with those diagnostics disabled is required. The release timeout
remains unchanged.

The 181.626-ms frame contains FIFO event 429200, renderer frame 39798, TID
21468. Of its 174.4506-ms body, **174.4365 ms is inside the command walker**;
reference-lock acquisition takes one QPC tick (0.1 microsecond). Draw callback
5781315 is wholly inside this drain on the same thread:

- Draw interval: QPC 14444006718118..14444008430722, 171.2604 ms.
- Resource resolution: **171.2049 ms**.
- Pipeline/constants: 0.0026 ms, existing PSO hit, zero compilation or shader
  disk load; geometry: 0.0013 ms; command recording: 0.0516 ms.
- Raw draw-thread cycle delta: 500,080. This is not CPU milliseconds and does
  not distinguish a driver wait, logging delay, or descheduling.
- No retained flush overlaps the draw. The flush ring retains all 1,247 events
  above its 2-ms threshold without overwrite. This excludes a recorded long
  `ld_flush` fence wait as the explanation, not sub-threshold flush activity.

The draw ring retains all 74 qualifying callbacks without overwrite. The FIFO
ring overwrites 2,834 older events; its retained extent contains all four windows,
but that alone is not a proof of complete threshold-event retention. The
identified event and nested callback are both present.

Resource resolution includes surface creation/lookup, sampled and vertex
textures, depth snapshots and descriptor handling. Source inspection finds no
per-lookup cache mutex; committed-resource creation, branch logging and thread
descheduling remain possibilities. Per-call resource attribution is the next
step, rather than assuming the entire interval is CPU texture conversion.

Evidence: `local/jungle-fifo-idle-abba-a1-{comparison-evidence,pump-report,
slow-analysis,draw-analysis}.json`, raw CSVs, window/camera records, and process
and resource observations. These findings narrow the next action; they do not
establish a frame-pacing fix or release promotion.

## Repeat without detailed renderer diagnostics

Run `20261013-230539-172`, PID 27544, repeats the same candidate and 1/0/0/1
sequence with `GCM_SLOW_DRAIN_CSV`, `RSX_SLOW_DRAW_CSV` and
`RSX_SLOW_FLUSH_CSV` disabled. Lightweight pump/present recording remains
enabled. A fresh private save and cache copy were used; the checkpoint and
finished injection animation were visually confirmed before timing. Four
20-second camera windows use tokens 3480-3483. All inputs detach before timing;
the game closes normally with exit 0 and the real-save baseline hashes match.

| Timeout ms | FPS | Mean ms | p99 ms | Max ms | Wait ms/frame | FIFO ms/frame | Drains/frame |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 79.291 | 12.612 | 18.353 | 25.353 | 6.898 | 3.765 | 10.73 |
| 0 | 74.606 | 13.404 | 20.809 | 33.989 | 0.172 | 11.131 | 644.74 |
| 0 | 76.386 | 13.091 | 20.510 | 34.242 | 0.164 | 10.830 | 646.58 |
| 1 | 79.216 | 12.624 | 19.354 | 31.974 | 6.884 | 3.784 | 10.67 |

Pooled normal-timeout windows: 3,168 frames, **79.254 FPS / 12.618 ms mean /
18.867 ms p99**. Zero-timeout windows: 3,018 frames, **75.496 FPS / 13.246 ms
mean / 20.771 ms p99**. Pump-thread CPU averages about 4.07/4.26 ms in the
normal windows and 10.89/10.82 ms with zero timeout. No wait failures or fallback
Sleep returns were recorded. All intervals, including three above 33.333 ms,
are included; no frame exceeds 50 ms.

Mode/camera checks pass. Strict isolation fails because Haze PIDs 23516 and
43004 overlap. Twenty resource samples bracket the windows with a 5.369-second
maximum gap, 52.36-53.89% memory use, and zero observed compilers. No owned
compile, screenshot or heavy analysis runs during timing. This is not an
isolated causal regression measurement. It does show that removing the detailed
drain profiler does not make the observed polling result favorable. Both return
windows remain close to their initial normal-timeout windows.

There is no evidence here to promote zero-timeout polling. Repeated empty or
blocked drains consume time instead of making the producer deliver commands
earlier. Queue snapshots alone cannot distinguish those two cases. The next
priority is resource-stall attribution and actual producer work, rather than
counting the complete wait scope as freely recoverable time.

Evidence: `local/jungle-fifo-idle-ordinary-a1-{comparison-evidence,pump-report,
pooled-modes}.json`, raw pump/present CSVs, window/camera records and observer
logs. The user's release launcher is unchanged.
