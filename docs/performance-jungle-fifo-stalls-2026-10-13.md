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

## Live nested capture: shader-cache loads and non-draw stalls

Run `jungle-pso-detail-a1` / `20261013-215131-381` uses the candidate above,
native fill/read/coherence/job-wait experiments off, and FIFO/flush/draw
diagnostics on. Automated menu navigation initially stopped at Continue Campaign;
the same owned process was navigated successfully afterward. A confirmation
marker records QPC 14405057124131 at 10 MHz. The tutorial briefly entered the
strong Nectar effect; neutral input and a subsequent capture verified the
settled jungle before timing. This is not a matched performance comparison.

Two 60-second camera windows retain all 7,867 complete intervals: **65.574 FPS /
15.250 ms mean**, p95 20.480 ms, p99 27.424 ms, maximum 818.050 ms; 34 frames
exceed 33.333 ms and 10 exceed 50 ms. Both owned game and measurement script
exit 0. Camera/mode checks pass; strict isolation fails. Twenty-five resource
samples bracket timing, with maximum gap 5.353 seconds, memory 52.82-53.75%,
up to two compilers, and other Haze PIDs 44672/48508. There was no owned
compilation or screenshot during timing. The seven private baseline hashes
remain intact.

The new draw CSV validates all nested durations and retains 106 slow draws
out of 6,912,959 callbacks, with zero overwrites. Fifteen occur before the
confirmation marker, 91 between confirmation and timing, and **none inside
either timing window**. This is a 4-ms threshold, not proof that drawing costs
nothing. Representative pre-timing loading draws are:

| Callback | Draw ms | get_pso ms | Shader disk-load scope ms | D3DCompile ms | Driver PSO creation ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| 531841 | 1961.256 | 1961.148 | 1946.636 | 0 | 14.344 |
| 954186 | 1582.092 | 1581.979 | 1568.012 | 0 | 13.858 |
| 530103 | 1290.264 | 1288.795 | 1280.047 | 0 | 8.661 |
| 532480 | 1280.509 | 1280.422 | 1271.364 | 0 | 8.973 |
| 530061 | 1185.247 | 1182.162 | 1170.261 | 0 | 11.776 |

All five are in-memory PSO lookup misses. Across the 91 retained loading draws,
the shader disk-load scope totals **36,272.809 ms**, compared with 215.277 ms
in D3DCompile and 659.581 ms in driver PSO creation. These are filtered elapsed
scopes, not a complete loading budget or CPU cost. The load helper includes
file open/read/close, allocation, blob creation, validation, progress logging,
and scheduling. Split those subphases before choosing a disk preload, archive,
or logging fix; do not label the entire interval physical storage latency.

Steady-gameplay stalls have a separate shape. FIFO retains 16,384 events with
78 older overwrites; its retained extent starts before confirmation and covers
both timing windows. Flush retains 2,118 events with zero overwrites. In the
818.051-ms pump frame, FIFO event 307159 spends 814.597 ms in the body and
0.0001 ms waiting for its lock. No retained draw or flush overlaps that event.
The 625.683-ms frame contains event 289959, a 620.183-ms FIFO body, again with
0.0001-ms lock wait and no retained draw. Its 2.851-ms flush starts after the
drain. Many subthreshold operations or descheduling remain possible.

Both long drains enter with GET equal to PUT. Existing periodic statistics
provide a concrete non-draw lead on the same FIFO thread:

| FIFO event | Begin QPC | VP-count print QPC | Difference |
| --- | ---: | ---: | ---: |
| 289959 | 14407265200308 | 14407265200429 | 12.1 microseconds |
| 307159 | 14407505204764 | 14407505204784 | 2.0 microseconds |

`gcm_vp_burst_poll` calls `fprintf(stderr, ...)` every two seconds while holding
the FIFO consumer lock, including fixed normal mode with profiling disabled.
This correlation does not time the print itself, but identifies avoidable
synchronous diagnostic output in the relevant path. Follow-up under #71/#35
will remove periodic reporting from fixed normal mode, preserve final totals,
test the actual header, and compare the resulting executable. Shader-cache
subphase attribution remains separate.

Private evidence: `local/jungle-pso-detail-a1-{comparison-evidence,draw-analysis,slow-analysis,draw-joins,pso-detail-analysis}.json`,
the load marker, matching original CSVs/logs/windows/observers, and setup captures.
