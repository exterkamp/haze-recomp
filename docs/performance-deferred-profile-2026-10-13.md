# Deferred diagnostic output

Tracked in [#9](https://github.com/zdiemer/haze-recomp/issues/9). SDK `cabcd04`
and root `d3e2693` remove the synchronous profiler output found in the
[renderer stacks](performance-renderer-blocking-2026-10-13.md). This improves
measurement quality; it does not change ordinary-build rendering performance.

Renderer and geometry CSVs now retain 32,768 complete rows each in bounded
memory (64 MiB plus 8 MiB). Storage is allocated and touched at first use.
Normal capture close writes the header and retained rows in chronological
order. Wrap discards only complete oldest rows; formatting failures are counted
and close reports recorded/retained/rejected counts and save success. A crash
or forced termination loses these captures. Rows still incur formatting and
timing overhead; this is not a zero-overhead profiler.

CSV mode suppresses duplicate renderer/pump text reports and the renderer's
periodic adapter-memory queries. Other runtime diagnostics remain unchanged.
Pump live-control cadence is preserved. Without CSV recording, existing text
reports remain available. No new user setting is required.

The full `build.ps1 -SpuFma -ProfileRenderer -Test` gate passes. A new test
exercises no destination file during recording, wrap/order, oversized and
unterminated row rejection, repeated/empty dump, invalid capacities, failed
output and destruction. Existing pump accounting/reset/ring tests pass too.

## In-game recorder validation

Candidate SHA256:
`376B0C1C5672F74E59B196AE985120394072A7CFEF17B77DA5B7E643787CA49F`.
Run `20261013-050458-527`, private carrier save, muted real audio, 1440p render
and follow-resolution shadows, 1280x720 window, VSync on/cap zero, GPU
occurrences 2, CPU flags 13571, native VP consumer on, producer/job polling off.
Capture `requested_011542.ppm` verifies the carrier before timed input.

The renderer output file is absent during playback. Normal close exits zero
and saves **12,626 consecutive renderer rows and matching geometry rows**:
identical frame/QPC keys, complete columns, zero rejected rows. Pump recording
retains 12,622 intervals. Both 24-second camera windows have complete joined
renderer/pump records; present/mode/camera checks pass. Duplicate renderer and
pump profile lines are absent from the runtime log.

**The isolation audit fails, correctly:** the user's interactive WIP (PID
62912) overlaps the diagnostic (PID 39184), and Ghidra analysis is active with
a two-CPU/4-GiB limit. These results do not support an ordinary-build or matched
before/after FPS claim. No observation or frame was discarded to pass that
gate. A separately named non-isolation report retains present/camera checks.

| Shared-load diagnostic | Sweep 1 | Sweep 2 |
|---|---:|---:|
| Frames | 341 | 346 |
| Present FPS | 14.24 | 14.49 |
| Mean ms | 70.249 | 69.009 |
| p95 ms | 94.514 | 107.194 |
| p99 ms | 115.036 | 162.711 |
| Maximum ms | 138.697 | 182.262 |
| Pump wait mean ms | 44.984 | 47.401 |
| Pump FIFO mean ms | 22.884 | 19.354 |
| Pump present mean ms | 1.720 | 1.687 |

The first worst pump frame (11939, 138.701 ms) contains 64.674 ms PSO
preparation, including 30.709 ms pipeline creation and 30.034 ms shader disk
load. The second worst (12234, 182.265 ms) spends 153.530 ms in the pump wait
scope. These are nested wall times under shared load, not removable CPU costs
or proof of a release regression. The earlier diagnostic output stalls were
real, but eliminating that output does not eliminate other pacing problems.

Private artifacts use `local/deferred-profile-*`; source runs and captures
remain uncommitted. The original user WIP remains available unchanged.

## Representative checkpoint follow-up

The user identifies their real resume point as a busy enemy area with a
10–30-second initial slowdown. Use it as the primary representative scenario:
measure initial resume/first-use work separately from settled gameplay, keeping
the carrier as a controlled historical comparison. A hash-verified immutable
private snapshot is recorded in `local/real-resume-baseline.json`; launch tests
from disposable copies. Do not modify the playable save or mix loading frames
into the settled-camera metric. State shader cache conditions explicitly.
