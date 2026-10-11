# Carrier profiling and controlled experiments, October 11, 2026

The target remains 30 actual gameplay FPS with 33.3 ms pacing. It is **not
achieved**. The results below distinguish measured stage savings from noisy
end-to-end differences. All native diagnostics were muted while retaining the
audio mixer, device clock and game audio events.

## Conditions

Windows, Ryzen 9800X3D, USA BLUS30094 carrier checkpoint, native D3D12 renderer,
AVX/FMA SPU build, persistent shader cache. Every timing session was visually
checked at the same stationary opening camera. NPCs and idle camera motion
continue, so draw counts and guest work are not identical between blocks.
`RSX_PROFILE_LIGHT=1` omits expensive legacy shader-variant diagnostics. GPU
timestamps measure GPU execution, not CPU submission or scheduling delays.
CPU scopes overlap: vertex/hash/pipeline work is included in draw preparation.

Each block contains 180 consecutive presented frames after at least 80 warm-up
frames. Captures, instruction sampling, builds and standalone microbenchmarks
were outside these blocks. Low-draw frames are retained in completed blocks;
silently filtering them would bias pacing measurements. A preliminary driver
stopped when it encountered such a frame; its four completed blocks remain
usable, and the driver was corrected before the remaining comparisons.

## Renderer CPU candidates

Run `20261011-214917-413`, with guest-call timeline recording disabled:

| Path | Frames | FPS | p95 ms | Reference construction ms | Remap ms | Texture checks ms | Mean draws |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Previous paths | 13667–13846 | 17.37 | 76.04 | 3.324 | 2.612 | 3.885 | 1441 |
| Bulk references | 14001–14180 | 18.58 | 73.36 | 1.492 | 2.509 | 3.877 | 1269 |
| Previous paths | 14301–14480 | 17.47 | 78.73 | 3.333 | 2.616 | 3.828 | 1373 |
| Direct dense remap | 14601–14780 | 17.78 | 74.19 | 3.272 | 2.233 | 3.857 | 1384 |
| Previous paths | 15971–16150 | 17.32 | 75.82 | 3.442 | 2.620 | 3.893 | 1478 |
| XXH3 texture checks | 16281–16460 | 19.12 | 70.91 | 3.167 | 2.536 | 3.659 | 1211 |
| Previous paths | 16581–16760 | 18.04 | 75.45 | 3.286 | 2.567 | 3.896 | 1335 |
| All three | 16881–17060 | 18.00 | 73.93 | 1.554 | 2.277 | 3.679 | 1451 |
| Previous paths | 17181–17360 | 14.49 | 90.55 | 4.107 | 3.018 | 4.095 | 1511 |
| All three | 17481–17660 | 16.67 | 78.53 | 1.681 | 2.411 | 3.766 | 1259 |

Bulk references reserve once per batch and fill the references directly, rather
than invoking a growth/capacity path for each vertex. Dense remapping uses the
exact bounded vertex ID when all references share a base; sparse/mixed-base
inputs retain the general lookup. These two paths are enabled normally because
their individual stage costs fell and reference-oracle tests pass. FPS gains
cannot be attributed entirely to them because the workload changes.

The experimental XXH3 path reads the complete texture contents, including tails.
Changing algorithms validates the previous identity before adopting the new
one, so a toggle neither invents texture mutations nor hides genuine changes.
The measured hashing saving was only about 0.2 ms/frame. It stays **off by
default**, alongside the previously experimental vertex payload cache. The
vendored BSD-licensed xxHash header records its version, source and checksum.

## Pipeline preparation

An earlier instrumented steady interval, frames 26960–27140 of run
`20261011-211854-560`, averaged 33.47 ms draw preparation, 12.66 ms vertex work,
4.22 ms pipeline preparation and 4.20 ms texture checking. GPU execution was
1.53 ms. Its linear PSO lookup used just **0.149 ms/frame**. Replacing that lookup
would address a small fraction of the measured cost. Shader compilation was
absent in this interval. Remaining pipeline work includes shader analysis,
structural hashing, live constants and render-state preparation; it needs
further subdivision before selecting a cache or parsing change.

## Command-buffer ownership and stutter

The earlier run logged an automatic title-ring rewind, invalid CALL words
containing `0xCECECECE`, and repeated reader resynchronization. One damaged
interval submitted 40,584 draws in an 846.97 ms frame, versus roughly 1,400
draws normally. This is actual command-stream corruption, not GPU saturation.

The consumer's fallback recycler now recognizes a synchronized callback even
before that callback enters its body. A producer at the ring end may be about
to call it; the consumer must not rewind current/PUT/GET in that gap. The new
regression fails against the old source at the pending-callback assertion and
passes with the fix. It also retains delayed-consumer, flip-insertion and
semaphore tests. Subsequent long native runs did not reproduce the automatic
rewind, invalid CALL or resynchronization messages. This repairs a verified
race; ordinary pacing remains well above 33.3 ms, so it does not explain or
resolve every stall.

## Deck-paint visibility

Run `20261011-211854-560` captured 40 consecutive frames with polygon bias
enabled and 40 with it disabled. Both clips use the same reference paint mask
and a homography fitted only to the nearby unpainted deck, avoiding moving
NPCs and paint influencing its own registration. The mask contains 5,324
interior paint pixels; eroded edges reduce camera-alignment artifacts.

| Metric | Bias enabled | Bias disabled |
| --- | ---: | ---: |
| Frames | 40 | 40 |
| Minimum paint visibility | 100% | 0% |
| Mean paint visibility | 100% | 0% |
| Median registration correlation | 0.995 | 0.991 |
| Mean temporal deviation, 8-bit channel units | 2.09 | 0.80 |

The four nearby yellow markings remain visible throughout the enabled clip
and disappear throughout the disabled clip. The lower deviation when disabled
is **missing paint**, not more correct rendering. Registration montages were
inspected locally. This validates the submitted polygon-offset correction in
this deck region; it does not establish that every texture, shadow, camera
angle or later mission renders correctly. GPU WARP tests separately exercise
zero, positive and negative bias on coplanar overlays.

## Guest waits and a rejected default change

The optional guest timeline captures syscall/import entry, exit, arguments,
guest thread and candidate saved return addresses. Nested intervals are merged
per host thread, so HLE calls containing syscalls are not double-counted.
Scheduling time is included; these are wall durations, not CPU utilization.
Raw stack slots must be checked against the generated lift, especially in
leaf functions. Instrumented observations must not be mixed with uninstrumented
FPS results.

In a separate 14.96 s trace, frames 11640–11904 of run
`20261011-215841-005`, the main thread spent at least 3.12 s inside recorded
service calls. Its 330 us sleep site traces through `0x00682574`, polling a
worker-status byte, with SPURS completion-queue traffic at `0x40400280`.
It is not evidence of a removable frame limiter. Condition 7 at LR
`0x00896C10` accounted for 741 ms across 93 waits, through saved returns
`0x008A3C54` and `0x005FB8A8`. Earlier long instrumented runs had much larger
waits; their cause and instrumentation overhead are not yet isolated.

An opt-in reusable Windows high-resolution timer was compared with the existing
yielding short wait. It preserves the requested delay and closes per-thread
handles on exit. On this machine a 330 us request averaged about 500 us in the
standalone duration test, so it is not assumed to improve pacing.

| Block pair | Existing FPS / p95 ms | Timer FPS / p95 ms |
| --- | ---: | ---: |
| 9297–9476 / 9621–9800 | 17.47 / 73.35 | 18.43 / 72.34 |
| 9921–10100 / 10221–10400 | 16.73 / 78.52 | 17.70 / 71.01 |
| 10521–10700 / 10821–11000 | 17.46 / 74.10 | 17.40 / 77.01 |

The result does not justify changing the default sleep backend.

## Native SPU history

Native instruction sampling identified compiled SPU routines and the drain
guard among the active CPU paths. The depth guard wrote a shared diagnostic
ring on every entry, racing across workers and mixing their histories. Normal
execution now reuses the existing per-thread history and retains the depth
limit. Eight concurrent synthetic workers verify isolation and that every
over-depth worker still halts.

Run `20261011-220524-777` compared a race-free approximation of the old shared
writes with private history. The relaxed shared stores approximate the former
cache traffic; they are not a bit-for-bit reproduction of its undefined data
race. The comparison includes live-control bookkeeping absent from normal use.

| Block pair | Shared FPS | Private FPS |
| --- | ---: | ---: |
| 14755–14934 / 15081–15260 | 19.15 | 18.69 |
| 15381–15560 / 15681–15860 | 18.78 | 19.50 |
| 15981–16160 / 16281–16460 | 19.14 | 18.24 |

There is **no consistent end-to-end speedup** in these samples. Retain the
correctness fix without counting it toward the 30 FPS target.

## Short-lived SPU jobs

Whole-process CPU accounting exposed a sampling gap: in one 15.04 s window,
the process accumulated 91.81 CPU-seconds while observed threads accounted for
46.83 CPU-seconds. Thread snapshots miss short-lived jobs; instruction counts
must not be treated as complete CPU utilization. More frequent snapshots still
had incomplete coverage, so worker-local cycle counters were added instead.

In run `20261011-221601-466`, frame boundaries 14600–15140 span 40.815 s.
There were 77,748 completed jobs, about 1,905 per second. This is a separate
logging-heavy diagnostic, **not an FPS benchmark**. Persistent jobs that do not
return are absent from these totals.

| Image | Completed jobs | Mean ABI wall ms/job | Setup share of setup + ABI cycles |
| --- | ---: | ---: | ---: |
| 30 | 36,206 | 0.572 | 5.16% |
| 40 | 540 | 24.039 | 0.11% |
| 42 | 1,371 | 7.455 | 0.30% |
| 25 | 13,225 | 0.510 | 4.57% |
| 41 | 6,480 | 0.957 | 2.35% |
| 33 | 2,160 | 2.427 | 1.08% |

Across all completed jobs, pre-ABI setup consumed 7.40 billion cycles and the
ABI run consumed 292.32 billion cycles: about 2.47% setup. This weakens the case
for optimizing allocation alone. The ABI interval still includes context
initialization, local-store copies, runtime helpers and translated game code;
it is not a measurement of guest arithmetic alone. Image 40 occurs about once
per frame and is a critical-path candidate, not yet a proven pacing cause.

The dispatcher currently creates a host thread per asynchronous job. Further
opt-in counters separate submitting-thread creation cost, ABI preparation,
native execution (including runtime helpers), and ABI completion. Worker reuse
must be justified with these measurements and preserve persistent-worker and
thread-local state semantics before becoming a default.

The finer diagnostic in run `20261011-222813-841`, boundaries 29050–29400
(20.177 s), recorded 45,291 completed jobs. Its intervals include instruction
sampling and must not be used as an FPS comparison. Inner ABI preparation,
execution and completion total 121.07 billion cycles against 207.77 billion
cycles around the whole ABI call: a 41.73% gap outside those inner scopes.
Submitting-thread creation consumed a further 8.95 billion cycles. These
scopes have slightly different boundary job counts and exclude persistent jobs.
Image 40 spent 99.85% of its inner ABI cycles in native execution and helpers;
image 30 spent 74.76%. Optimizing only explicit context initialization would
miss both the call-entry gap and the long job's execution cost.

Object-code inspection confirms that `spu_run_lifted_job_abi_profiled` invokes
`__chkstk` for a `0x41A98`-byte stack frame **before** its first internal cycle
counter. A no-op ABI diagnostic running twice on each of 1,000 fresh threads
measured 456,028 mean cycles for the first call and 25,146 for the second.
The outside/inside gap fell from 410,751 to 1,611 cycles. This isolates a
substantial first-use stack cost in the microbenchmark; it does not establish
that all of the in-game gap is stack growth or predict an FPS gain. Stack or
worker reuse now has a measured rationale, pending an in-game A/B test.
The diagnostic explicitly prevents inlining so the large stack allocation
remains inside its timed call. Run it with `scripts/benchmark_spu_abi.ps1`.
The normal build also retains a `__chkstk` prologue for a `0x41A88`-byte ABI
frame, so the large entry allocation is not introduced by the phase counters.

The frequent-snapshot sampler also recorded 826 distinct thread IDs but 908
failed opens (`ERROR_INVALID_PARAMETER`) in 10.05 s: many threads disappear
between the system snapshot and `OpenThread`. Only 27 lifetimes were opened.
This confirms that its missing CPU must not be attributed to the persistent
threads visible in instruction samples. Worker-local phase counters are needed
alongside sampling.

## Repeating the measurements

Build with `scripts/build.ps1 -SpuFma -ProfileRenderer -Test`. Set
`RSX_PROFILE_LIGHT=1`, `RSX_PROFILE_CSV=renderer.csv`, and point
`RSX_CPU_WORK_FILE` at an ignored text file. After visually verifying the
checkpoint, run `scripts/benchmark_cpu_work.py <csv> <control> --output <json>`.
Flags are 1 for bulk references, 2 for dense remapping and 4 for XXH3; zero
selects the previous renderer paths. Bit 8 selects timer waiting when
`PS3_USLEEP_MODE_FILE` uses the same file. Bit 16 selects private history when
`SPU_DEPTH_RING_FILE` uses it; private history is always used without that
diagnostic control. The driver restores zero on exit, so restore the desired
flags or remove the diagnostic environment before ordinary play.

Use the saved block frame bounds with `scripts/summarize_profile.py`, passing
`--min-draws 0 --last 0`. `scripts/profile_native_cpu.py` reports whole-process
CPU time as well as sampled threads; snapshots can miss short-lived jobs.
For completed-job phase counters, set `SPU_JOB_TIMING=1` in a profiling build
and use `scripts/summarize_spu_jobs.py <stderr.log> <renderer.csv> --first N
--last N`. CPU cycles are not milliseconds; scope totals overlap and cannot be
added to draw preparation or frame times.
For a separate wait trace, set `PPU_WAIT_PROFILE` to an existing ignored
directory. `PPU_WAIT_STACKS=1` adds candidate saved return addresses.
`PPU_WAIT_PROFILE_CONTROL` optionally toggles recording with a 0/1 text file;
threads check it at most once per second. Summarize with
`scripts/summarize_waits.py <directory> <renderer.csv> --first N --last N`.

For visual checks, configure captures through `run.ps1`, then use
`scripts/capture_sequence.py`. Analyze both bias clips with
`scripts/analyze_deck_sequence.py`, passing the same `--reference` image.
The analysis requires numpy and opencv-python-headless. Capture files, game
assets and generated translations remain ignored and are not published.

Next profiling priorities are the worker-status dependency and the remaining
shader analysis/vertex decoding costs, including complete CPU accounting for
short-lived native jobs. These results support continuing the existing fork;
they do not justify restarting the framework.

## Final normal-build verification

After the diagnostic runs, the executable was rebuilt with `-SpuFma -Test`
and profiling disabled. The full regression suite passed. Muted run
`20261011-223828-264` reached the carrier checkpoint; capture 8368 was visually
verified. Its ordinary five-second FPS reports included 17.5, 18.3 and 19.0,
consistent with the remaining performance gap. This was a smoke check, not
another controlled optimization comparison. No FIFO invalid CALL/recovery
messages appeared, and the owned game process closed cleanly with exit code 0.
