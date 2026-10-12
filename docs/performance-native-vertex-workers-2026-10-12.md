# Native parallel vertex conversion experiment — 2026-10-12

The [geometry cache experiment](performance-native-geometry-cache-2026-10-12.md)
validated reuse but did not establish a meaningful repeatable FPS gain. This
experiment uses additional native CPU threads to convert large changing meshes.
The objective remains consistent frame pacing and unlocked framerate with a
VSync option. A 144 FPS frame allows 6.94 ms; this experiment does not itself
establish that target or stable 30 FPS.

## Implementation

`libs/video/rsx_live_vertex_workers.h` implements a renderer-owned pool of up to
three persistent native Windows threads. `RSX_VERTEX_WORKERS=0..3` selects extra
workers in addition to the renderer; the default is zero. The optional
`RSX_VERTEX_WORKERS_FILE` accepts live 0..3 changes at the existing 60-present
control polling boundary. `RSX_VERTEX_WORKER_MIN` selects the minimum unique
vertex count, default 4096. Smaller draws retain serial conversion.

Workers convert disjoint contiguous reference/output ranges with the existing
native format/SIMD kernels. The renderer converts the remainder while they run,
then joins every submitted job before changing draw data or submitting it to
D3D12. Partition boundaries use multiples of 64 vertices when possible to avoid
output cache-line sharing. The renderer's MXCSR rounding/denormal settings are
carried into every worker job. The pool is lazy, retained across mode changes,
and joined/closed at shutdown. Creation failure retains serial conversion.

Only prepared contiguous attribute mappings with ordinary frequency are
eligible. The complete reference range is checked before dispatch. Workers
never call a guest resolver, touch mutable RSX/D3D state, or submit GPU commands.
Inline and resolver/divider-dependent paths remain serial. CPU source data and
references must retain their existing draw-read lifetime. This does not remove
reference gathering, remapping, GPU-ring copies, or command-producer work.

`RSX_VERTEX_WORKER_VALIDATE=1` independently converts the draw serially, compares
all output bytes/results, and uses shared native source-page generations before
and after both reads. Intervening writes count as unstable audits. A stable
mismatch disables parallel conversion. Audit mode publishes serial output and
is intentionally unsuitable for speed measurements. It requires the registered
native write tracker; unavailable validation sources use serial conversion.

CSV counters record total converted unique vertices, vertices/draws at least
4096, parallel vertices/draws/jobs, serial bypasses, audit/race/failure counts,
and renderer time spent joining after its own chunk. The join timer is nested
inside decode time and must not be added to it. The large-vertex fraction uses
unique converted vertices for both numerator and denominator, unlike the
existing expanded-topology `vertices` field.

## Validation

The full runtime test suite passes. The focused native worker oracle compares
252 parallel conversions against serial conversion across all seven vertex
formats, component sizes, SIMD mode combinations, 1/2/3 workers, shuffled and
20-bit-wrapped references, disabled-attribute defaults, nondefault MXCSR,
mutations, output guard bytes, and serial mode switches. Resolver calls are
checked against the renderer's host TID. Missing stable sources and divider
paths remain serial. The pool is destroyed and recreated to exercise lifetime.

An intervening source mutation must count as an unstable audit without disabling
workers. Deliberately corrupted output with stable source generations must
disable workers and publish the serial result. The intentional mismatch log in
that test is expected. Present-window tests cover 0/1 and 1/3 worker markers,
including the existing rejection of mode changes within measured windows.

The final disabled-attribute and unstable-source cases were also run through the
focused live-texture/GPU test after the full suite, followed by the Python
present-window tests. One standalone compile initially selected the x86 tool
against the x64 runtime library; rerunning with the repository's x64 compiler
passed. The game candidates use the normal build script and x64 compiler.

## Gameplay setup and evidence

All runs use private copies of the carrier save, muted audio with normal device
timing, 2560×1440 rendering, 1280×720 window, AA off, AF 1, shadows following
resolution, VSync on, and frame limit 0. Native FIFO, queue and SPU idle waits,
the paired cursor kernel, regional texture tracking, and renderer CPU-work 5379
remain fixed. Both prior geometry and decoded-payload caches are off. Profiling
uses `RSX_PROFILE_LIGHT=1` to avoid the known diagnostic shader-table scan.

Diagnostic candidate SHA256:
`8575FFC175881916299D5F913CFA5AE200BBB0B6F384469623E820D969AFB02F`.
Ordinary candidate SHA256:
`9A3797513E8580CDD0F163144B27024776D8BFAA846FB9AC59FDF18A85AF7FE0`.

Live audit run `20261012-210425-411` closed normally, exit 0. Whole-CSV counters
and the close summary agree: **17,610 parallel draws, 153,811,219 converted
vertices, 17,610 independent serial audits, zero unstable audits, zero failures**.
Viewed capture 021859 establishes the initial carrier gameplay scene; the first
navigation attempt's capture 010926 was still the title screen and contributes
no gameplay FPS claim. The settled audit window is frames 22210–22569, 360
frames after 80 warmup frames. Validation adds source tracking and serial work,
so its FPS is not a performance comparison.

Within that settled window, approximately 457,992 unique vertices are converted
per frame; 173,747 (37.94%) belong to draws with at least 4096 vertices. There
are approximately 19.88 such draws per frame out of 1430.11 conversion calls.
This measures how much work this threshold can reach, not its share of elapsed
decode time. Subthreshold work remains substantial.

Movement was also exercised with validation enabled again after the recorder's
automatic control reset. Capture 023056 shows the earlier movement near a
vehicle; final capture 023433 is black. The final frames 23442–23444 take
355.600/288.686/348.490 ms, while draw CPU is 0.804/0.741/0.896 ms and GPU time
0.506/0.314/0.316 ms. They contain no parallel jobs or large conversion draws.
The reason for this black/slow state remains unproven; it does not establish
mission completion or broad visual correctness. It is a concrete lead to work
outside draw preparation, rather than evidence that parallel conversion caused
or fixed the slowdown. No instruction sample was taken in that state before
the instance closed.

Private evidence: `local/native-vertex-workers-audit-{run,blocks,summary}.json`,
renderer and present CSVs, captures, and the normal-close log. None of the game
assets, captures, translations, saves, or binaries are committed.

## Worker-count profiling

Run `20261012-211033-190` completed all eight 180-frame blocks with 80 warmup
frames, using counts `0,1,2,3,3,2,1,0`. The fully closed CSV contains 78,535
parallel draws and 691,131,563 parallel vertices, with zero reported failures.
These are non-audit counts, not additional independently verified conversions.
The process observer brackets every measured block with gaps below 2.18 seconds;
only this owned Haze process was observed. Other application activity is not
excluded.

| Workers | Frames | Decode ms | Join ms | Pack ms | Diagnostic FPS |
|---|---|---:|---:|---:|---:|
| 0 | 12861–13040 | 3.587 | 0 | 10.697 | 23.06 |
| 1 | 13161–13340 | 3.562 | 0.334 | 9.573 | 16.44 |
| 2 | 13461–13640 | 3.318 | 0.397 | 9.099 | 18.84 |
| 3 | 13761–13940 | 2.964 | 0.357 | 8.129 | 26.37 |
| 3 | 14033–14212 | 2.956 | 0.316 | 8.339 | 25.78 |
| 2 | 14361–14540 | 3.155 | 0.330 | 8.661 | 25.29 |
| 1 | 14661–14840 | 3.401 | 0.271 | 9.089 | 22.42 |
| 0 | 14961–15140 | 3.744 | 0 | 8.987 | 25.27 |

Three workers have the lowest observed decode time, including their join cost.
However, viewed captures 011220 and 015711 show different carrier viewpoints.
The user was also trying the open game during this run; input timing relative
to each block was not recorded. Converted unique vertices fall from about
597,215 in the first serial block to 477,971 in the last, versus about
463,452–468,975 in the three-worker blocks. Decode per unique vertex is
6.01/7.83 ns for serial versus 6.40/6.30 ns for three workers. Format mix and
scene activity also vary. Consequently, the lower absolute decode/pack times
are preliminary leads, not an established controlled speedup. The ordinary
comparison is recorded separately with private scripted input.

## Later gameplay and CPU sample

After the stage blocks, the worker control was reset to zero and movement was
exercised. The user reported roughly 90 FPS after the initial deck/conversation
and normal-speed movement, animation, and dialogue. This is useful playtest
evidence for unlocked gameplay in that section, not broad simulation validation
or an attributable gain from vertex workers, which were disabled then.

Viewed capture 023763 shows a later squad conversation inside the vehicle;
this attempt did not remain in the prior audit's black/slow state. Automated
movement and the user's playtest overlap, so it is not a deterministic route
reproduction. Later CSV regions have much less draw preparation than the deck.
The later sample is intentionally outside every FPS comparison window.

A 12.008-second instruction sample selected host TIDs 51768, 53896, 37936,
and 37720. The process consumed 43,984 ms CPU; selected threads account for
27,812 ms. Suspension overhead totals 1,150 ms, so the sample itself perturbs
execution and its FPS must not be used as a benchmark. RIP frequencies include
waits and are not CPU percentages.

The producer's repeated stacks traverse `func_00682538` → `func_0068EFC0` →
`func_0071EFE8` → `sys_timer_usleep`; the other hot PPU thread repeatedly reaches
`sys_timer_usleep` from `func_00B73738`, alongside real SPURS event waits.
These support the existing job-completion/polling lead, not removal of a
proven artificial frame cap. The renderer still samples reference remapping,
vertex conversion, texture write tracking, GPU flush waits and FIFO idle waits.
Additional native work must preserve the completion predicates and publication
ordering. No timer or wait semantics changed in this experiment.

Inspection of the generated `func_00B73738` identifies a concrete syscall site
at `0x00B73A1C`: a requested 800 µs sleep repeats while shared buffer indices
have not progressed sufficiently. The indices and wrapped distance through
eight entries are read again on each iteration. This merits tracing the buffer's
publisher before replacing the poll with a native completion notification;
its subsystem identity and effect on frame-critical work are not established.

Private artifacts: `local/native-vertex-workers-stage-{run,blocks,summary}.json`,
stage CSVs/process observations/captures, and
`local/native-vertex-workers-slow-route-{hot,cpu}.json`.

## Ordinary successful-present comparison

Ordinary run `20261012-212134-657` closed normally, exit 0. Viewed captures
011231 and 024879 retain the same carrier viewpoint. NPCs continue moving,
and the known model-visibility issues remain visible in the latter capture;
this experiment does not establish graphical correctness.

The first sequence uses 5-second warmup and 45-second measurements; the reverse
uses 5-second warmup and 30-second measurements. Every complete present interval,
including stalls, is retained. Both process logs bracket all blocks with gaps
below 2.12 seconds and observe only the owned process. No builds, captures or
instruction sampling occur inside those windows.

| Sequence | Workers | Intervals | FPS | Mean ms | P95 ms | P99 ms | Max ms |
|---|---:|---:|---:|---:|---:|---:|---:|
| First | 0 | 1379 | 30.676 | 32.598 | 45.674 | 51.342 | 66.767 |
| First | 3 | 1402 | 31.177 | 32.074 | 45.114 | 50.791 | 64.735 |
| First | 3 | 1389 | 30.870 | 32.394 | 45.182 | 51.045 | 85.543 |
| First | 0 | 1342 | 29.855 | 33.495 | 47.314 | 52.987 | 64.342 |
| Reverse | 3 | 961 | 32.049 | 31.202 | 43.605 | 48.319 | 52.790 |
| Reverse | 0 | 931 | 31.052 | 32.204 | 44.817 | 49.964 | 90.137 |
| Reverse | 0 | 876 | 29.222 | 34.220 | 48.122 | 53.478 | 64.933 |
| Reverse | 3 | 864 | 28.844 | 34.669 | 47.678 | 52.711 | 82.540 |

Equal weighting of each mode's block means gives 33.047 → 32.234 ms in the
first sequence (2.46% lower), and 33.212 → 32.936 ms in reverse (0.83% lower).
The reverse's last worker block is slower than its adjacent serial block;
scene drift and ordinary variation remain larger than parts of this difference.
This is a small, inconclusive total-frame benefit, not a decisive multicore
breakthrough. P95 remains approximately 44–48 ms, so consistent 30 FPS pacing
is not achieved. Keep workers off by default.

Private evidence: `local/native-vertex-workers-ordinary-{run,windows,report}.json`,
`local/native-vertex-workers-ordinary-reverse-{windows,report}.json`, ordinary
present CSV, both process logs, normal-close log and captures. The baseline
already includes earlier native optimizations; the new worker experiment must
not take credit for those gains or the user's later 90 FPS playtest.

## Reproduction

Build profiling and ordinary candidates separately, outside measured windows:

```powershell
./scripts/build.ps1 -SpuFma -ProfileRenderer -Test -OutputDirectory local/candidates/native-vertex-workers-profile
./scripts/build.ps1 -SpuFma -OutputDirectory local/candidates/native-vertex-workers
```

Launch with a private save/input fixture and the environment above. In-game,
verify the carrier view before recording. For stage timings, set a separate
worker control file and run `scripts/benchmark_cpu_work.py --mode-column
vertex_workers` with at least 80 warmup and 180 measured frames per worker count.
For total-frame evidence, use `scripts/benchmark_present_modes.py record
--mode-column vertex_workers` with timed settled blocks in an ordinary binary,
then close only that owned instance normally and report its fixed-ring CSV.
Record other Haze processes read-only; preserve contended results with explicit
limitations. Screenshots, builds, and instruction sampling stay outside timed
windows. Do not discard slow frames.
