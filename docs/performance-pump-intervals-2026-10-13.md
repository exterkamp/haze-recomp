# Per-present render-pump attribution

Tracked under [#9](https://github.com/zdiemer/haze-recomp/issues/9) and
[#39](https://github.com/zdiemer/haze-recomp/issues/39).

`HAZE_PUMP_CSV` adds a bounded 16,384-interval diagnostic ring to the render
thread. Each row spans two consecutive guest-present returns, partitioning
wall time into completed FIFO drains, event waits, presentation, vblank/flip
ticks and the remaining loop work. The startup interval is discarded. The
existing two-second reporter has independent counters, so its reset cannot
split or lose frame accounting. Normal window close writes the ring; abrupt
termination does not. No file IO occurs per frame. Without the environment
variable, the ring and thread-CPU query are inactive.

Thread CPU comes from GetThreadTimes, sampled just after each end timestamp.
Windows accounting is coarse (15.625 ms steps on this run), so individual-row
CPU is not an exact scheduling trace. Aggregate CPU is useful; it is not total
process CPU. The event-wait scope also includes the queued-work query, and
queued-wait counts do not establish whether a wait slept.

`scripts/summarize_pump.py` checks consecutive intervals, QPC frequency,
nonnegative/finite scopes, accounting conservation and window coverage. It
joins exactly one renderer profile timestamp inside each pump interval. The
renderer has a different startup frame offset, so frame numbers are not used
for this join. Renderer scopes are nested/overlapping, and GPU time overlaps
CPU: none may be added to the pump partition.

`scripts/correlate_pump_waits.py` clips PPU service records to those same QPC
intervals and unions nested scopes per host thread. Caller groups overlap;
host-thread unions must stay separate when interpreting wall time. Calls
shorter than 200 us are absent from the source recorder, making these lower
bounds. Overlap alone does not prove a causal dependency.

## Validation

The full `build.ps1 -SpuFma -ProfileRenderer -Test` suite passed, including
actual-header tests of startup exclusion, partial drains, consecutive
presents, reporter resets, CPU invalid/recovery handling and ring wrap/dump.
The Python summary and wait-correlation suites pass 11 and 6 tests respectively:
missing/duplicate joins, clock mismatch, incomplete coverage, invalid scopes,
cross-frame clipping, nested services and multiple host threads are covered.
Both are now in the standard build test gate. Source review found no current
accounting or join blocker.

Diagnostic binary SHA256:
`49C6708B4472066FD849FE82E04EF59A3C00F438059CBC6D396896DD8EFEA575`.
Run `20261013-033644-929`, private prefix `local/pump-critical`, used muted audio,
private input/save state, 1440p/follow shadows, a 1280x720 window, VSync on,
no frame cap, GPU occurrence mode 2 and native index preparation. Native VP
consumer was fixed on; producer batching and native job wait were off. The
renderer light profile and PPU wait profile were enabled. This is a diagnostic
run under shared host load, not an ordinary FPS comparison. Labyrinthine was
still running; it was closed afterward for a separate ordinary comparison.

## Measured partition

Two 24-second carrier camera sweeps followed five-second warmups. The scene
was visually checked first. All presents, mode events, actual camera angles,
process brackets, pump accounting and renderer joins pass validation.
The owned process closed normally; 14,968 pump intervals were retained without
ring truncation. Only complete intervals inside the measurement windows are
summarized below (746 and 733).

| Mean wall scope, ms | Sweep 1 | Sweep 2 | Frames over 50 ms, sweep 1 | Frames over 50 ms, sweep 2 |
|---|---:|---:|---:|---:|
| Frame interval | 32.113 | 32.686 | 53.113 | 56.243 |
| Event-wait loop | 14.176 | 14.067 | 27.498 | 30.862 |
| FIFO processing | 16.140 | 16.798 | 23.531 | 23.116 |
| Present | 1.770 | 1.802 | 2.047 | 2.130 |
| Tick/other | 0.028 | 0.018 | 0.037 | 0.136 |
| Frame count | 746 | 733 | 11 | 14 |

The two p95 intervals are 44.30/45.40 ms and p99 51.12/55.13 ms. No interval
over 100 ms occurred in these windows, so this run does not explain the earlier
100+ ms tails. The slowest 65.709 ms frame contains 47.458 ms in the wait loop,
16.208 ms in FIFO processing and 1.990 ms presenting. Its nested GPU scope is
2.663 ms. Across all frames GPU execution averages 2.249/2.359 ms; draw CPU
scopes average 12.893/13.514 ms and vertex preparation 1.370/1.373 ms.
Render-thread CPU averages 13.216/13.728 ms. These are diagnostic observations,
not a claimed FPS improvement over another run.

Guest main thread 1 maps to host TID 22524; its recorded service timestamps
bracket both windows. Its service union averages 5.994/6.391 ms per frame,
increasing to 13.663/15.575 ms in frames over 50 ms. The dominant caller is
`sys_timer_usleep` at LR `0068EFF0`, requesting 330 us: 13,042/13,671 clipped
call overlaps and 4427.206/4636.421 ms total. Main-thread completion polling
and renderer waiting both increase in slower frames. This strengthens the
job-delivery lead under #39, but it does not establish which worker or queue
determines readiness. No condition-7 stall was reproduced in these windows.

## Next targets

Attribute job readiness/publication rather than simply removing waits; the
previous shorter-poll experiment has mixed whole-frame results. Independently,
FIFO processing still exceeds the entire 6.94 ms budget. Retiring its duplicate
legacy graphics-state mirror is a candidate under #35, with reference/label
writes preserved. Source inspection alone supplies no timing gain. In current
Windows Haze the null window backend remains registered even after live D3D12
initialization; a `rsx_get_backend()==NULL` shortcut would therefore never
activate. Any retirement needs an explicit ownership transition and tests of
the window, diagnostics and guest-visible side effects.
