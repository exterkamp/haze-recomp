# Jungle FIFO stall diagnostic

Tracking: #38 (native memory replacements), #32 and #9 (frame pacing).

The ordinary candidate from Haze `ce30279`, SDK `17eb9d3`, was measured with
the inline-read shim excluded and the inline reservation predicate disabled.
Executable SHA-256:
`CED4A04F8C74BB629E8ABF5A2B7712B5A37B80E8F217E15443D431DB5FCD6AA1`.
This is a diagnostic run, not a speedup comparison: FIFO and flush tracing
were enabled, and shared-host activity remained present.

## Capture conditions and retention

`jungle-slow-render-a1` used a fresh private copy of the real jungle save,
visually confirmed after the Nectar tutorial; 1440p, 8192 shadows, VSync off,
uncapped, GPU mode 2, CPU mode 13571, native VP consumer and worker reuse on,
native producer and native job wait off. Audio output was muted. After setup,
input-file polling was acknowledged detached. Two 60-second absolute camera
sweeps followed five-second warmups. Both the measurement helper and owned
game exited normally with code 0. No own compile, screenshot or CPU sampler
ran inside the measured windows.

Camera and mode checks passed. Strict process isolation failed: other Haze
PIDs 36484 and 62060 overlapped; up to one external compiler was observed.
Twenty-six resource samples bracketed the windows with a maximum 5.377-second
gap; memory usage was 53.64–55.41%. Sparse sampling cannot exclude other
activity between observations.

All 7,978 measured frames remain included: **66.495 FPS / 15.039 ms mean**,
19.781 ms p95, 23.748 ms p99, 158.932 ms maximum; 23 frames exceeded 33.333 ms
and two exceeded 50 ms. These numbers include diagnostic overhead and must
not be treated as the ordinary binary's isolated performance.

The FIFO ring retained all 15,131 events over 2 ms; the flush ring retained
all 1,903 events over its 2 ms threshold. Neither ring overwrote an event.
Within the exact timing windows, 8,306 retained FIFO and 822 flush events
were fully contained. Maximum lock wait among those retained FIFO events
was 0.0013 ms; maximum retained flush was 6.733 ms. The thresholded event
rings do not provide complete accounting of subthreshold work.

## Two distinct long-frame shapes

| Pump frame | Total ms | Outer wait ms | FIFO ms | Present ms |
|---|---:|---:|---:|---:|
| 44295 | 158.9325 | 6.7360 | 148.1723 | 4.0160 |
| 42195 | 89.5497 | 79.8782 | 6.8216 | 1.9536 |

For frame 44295, FIFO event 248050 on host TID 57272 lasted **147.8847 ms**:
0.0002 ms lock wait and **147.8845 ms inside the drain body**. Its QPC interval
was 14374891303127–14374892781974 at 10 MHz. The overlapping pump frame's
only retained flush, event 1845 on the same TID, began at 14374892782638,
after that drain ended. It lasted 3.5975 ms, including 3.4550 ms fence wait.
Thus this recorded flush does not explain the 148 ms FIFO phase. Absence of
a nested retained flush does not exclude subthreshold flush work.

The drain reported 153,388,260 raw thread cycles. These are not converted to
CPU milliseconds or a CPU percentage. The scope includes rendering callbacks
and any blocking or descheduling inside them; it is not proof of 148 ms of
active command-decoding work.

Frame 42195 instead recorded 71 outer waits and 77 drains, with 79.8782 ms
in the wait phase. Its retained FIFO event lasted 5.2605 ms. This is a
separate producer/work-availability or scheduling investigation, not the same
shape as the long drain. Timing alone does not establish the upstream cause.

## Next measurements

Expose the existing bounded slow-draw recorder in an ordinary build. It is
currently compiled only with broad renderer profiling, despite having a
separate diagnostic environment selector. Its four draw stages can separate
geometry, pipeline/constants, resource resolution, and command recording.
Join by QPC and host TID, not renderer versus pump frame numbers (which differ).
Retain overwrite counts and thresholds; many short draws and non-draw FIFO
methods can remain unaccounted for. Keep the outer-wait investigation separate.

The native memory-fill experiment proceeds independently from sparse caller
samples; this capture does not attribute the FIFO stall to that fill.

Private evidence: `local/jungle-slow-render-a1-{comparison-evidence,slow-analysis,hitches}.json`,
the corresponding FIFO/flush/pump/present CSVs, windows manifest and process/
resource observers. `scripts/analyze_slow_drains.py` validates the rings and
joins QPC intervals; the private summary retains raw phase values for the
worst timed frames.

## Ordinary PSO attribution follow-up

The ordinary draw recorder and normal-exit export are now verified; see the
[slot-fill report](performance-native-slot-fill-2026-10-13.md) for the initial
failed export, its fix, and the actual 61-row gameplay capture. Startup draws
spent 15-41 ms in pipeline/constants. The first pair of shaders already hit
the bytecode disk cache, so shader compilation cannot be assumed to explain
that draw. Later ordinary captures also retain long FIFO intervals without
enough detail to establish their cause.

SDK `b852879` extends the existing opt-in recorder with:

- `get_pso_ms`, nested inside pipeline/constants;
- `pso_lookup`: 0 before the actual lookup is reached, 1 for an in-memory key
  hit (including a cached failed PSO), 2 for a miss (including cache-full or
  subsequent build failure);
- `build_pso_ms`, nested inside `get_pso_ms`;
- `shader_disk_load_ms`, `d3d_compile_ms`, and `create_graphics_pso_ms`,
  nonoverlapping subscopes inside `build_pso_ms`.

The four original stages still partition draw elapsed time. Nested values
must not be added to that partition. Explicit scope arguments associate work
with the current draw; disabled diagnostics add no QPC/cycle queries. The
1,024-record ring, threshold, overwrite reporting, and normal-exit export
remain unchanged. Timings include blocking and descheduling inside each scope.

The analyzer accepts historical, lookup-only, and full-detail CSVs. It rejects
incomplete detail schemas, invalid outcomes, nonfinite durations, and nested
durations exceeding their parents. Twelve Python tests pass, including actual
decimal-valued full-schema CSV parsing. Actual-source ordinary/profile fixtures
pass with disabled-clock, early-return, retention/overwrite, phase conservation,
and nested export checks. The first extended fixture needed a floating-point
comparison tolerance for 0.1 + 0.1 + 0.1 versus 0.3; the corrected test uses the
same rounding tolerance as the analyzer. Runtime scope placement was reviewed;
the fixture does not execute a real D3D12 pipeline creation.

The incremental ordinary build with two jobs exited 0. Candidate
`local/candidates/slow-pso-detail/HazeRecomp.exe` has SHA-256
`691B0D8B732F4D21ABFC78812D0118A8A03D840F6C66CFC3FF78DBE611FE71BF`.
The next live capture will establish which nested phases actually dominate;
this instrumentation is not itself a frame-rate improvement.
