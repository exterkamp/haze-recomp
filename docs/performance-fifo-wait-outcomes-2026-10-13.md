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

Live phase validation and the scheduling comparison are pending. No speedup,
frame-pacing fix or release promotion is claimed by this instrumentation commit.
