# Native arithmetic and wait follow-up, 2026-10-12

This follows the [initial arithmetic experiment](performance-native-spu-float-2026-10-12.md)
on the experimental branches, based on main `a872469` and SDK haze `9d282dd`.
The prior goal turn made progress by implementing, validating and measuring
native arithmetic. It did not establish consistent pacing or the high-rate goal.

## Wait interaction measured first

Muted run `20261012-222050-278`, owned PID 57916, uses the previous ordinary
candidate, SHA256 `B39A8292FAD3531A6F51973C7D12CDA83615015BE814876D32CFC0C5F7BB5B8F`.
Rendering is 2560×1440, window 1280×720, shadows follow rendering resolution,
AA off, filtering 1, cap 0 and VSync on. The private checkpoint/input and native
cursor/FIFO/queue/idle/texture-watch settings match the preceding experiment.
Vertex workers and geometry caches remain off. Native arithmetic stays on,
validation and job logging off, for every block.

The existing `PS3_USLEEP_MODE_FILE` selects yielding deadline polls (0) or the
reusable high-resolution timer (8). This affects waits of at least 50 µs across
PPU threads, including both the audio producer and main producer; it is not
an audio-only change. Native events for main host TID 22300 and audio TID 2708
prove each mode settled before the measured blocks. Arithmetic mode events
and the final summary prove arithmetic remains enabled without validation.

Capture 010386 establishes the stationary carrier before the windows and
021419 confirms the viewpoint afterward. NPCs and scenery still change over
time, including known visual defects. Each block has eight seconds of warmup;
first-order measurements last 40 seconds, reversed ones 30 seconds. Captures,
builds, instruction sampling and the later thread-counter measurements are
outside these windows. Only the owned Haze appears in the bracketed process
observations, with gaps below 2.19 seconds. Other applications and very short
process lifetimes between samples are not excluded. Normal close returns 0.

| Order | Timer flags | FPS | Mean ms | p95 ms | Mean CPU cores |
| --- | ---: | ---: | ---: | ---: | ---: |
| First | 0 | 23.837 | 41.952 | 59.959 | 5.469 |
| First | 8 | 23.234 | 43.040 | 60.899 | 5.010 |
| First | 8 | 23.533 | 42.493 | 60.185 | 4.814 |
| First | 0 | 21.957 | 45.543 | 67.054 | 5.355 |
| Reverse | 8 | 23.114 | 43.263 | 61.114 | 4.771 |
| Reverse | 0 | 20.047 | 49.883 | 72.127 | 5.085 |
| Reverse | 0 | 19.164 | 52.181 | 73.859 | 5.204 |
| Reverse | 8 | 20.221 | 49.453 | 70.626 | 4.817 |

First-order block-average frame time improves 43.747 to 42.767 ms (2.24%);
reverse improves 51.032 to 46.358 ms (9.16%). The changing baseline and tails
prevent promising a stable pacing gain. These absolute results also differ
from the previous launch of the same candidate; do not attribute that cross-run
difference to the timer. Keep the timer opt-in and continue investigating the
remaining work and drift. Neither consistent 30 FPS nor the full goal is met.

## CPU counters without suspension

After both FPS comparisons, `scripts/measure_thread_cpu.py` retains handles
to the explicitly selected owned threads, verifies their process/executable
and live creation identity, then reads CPU time and cycle counters. It does
not suspend them, change priority or affinity, or sample instruction pointers.
The control file changes an already configured mode. Native mode events must
be checked separately; they pass for each interval and selected worker here.
Each CPU interval lasts eight seconds after three seconds of settling.

| Timer flags | Main CPU ms | Main cores | Audio CPU ms | Audio cores |
| --- | ---: | ---: | ---: | ---: |
| 0 | 6093.750 | 0.7617 | 3562.500 | 0.4453 |
| 8 | 4734.375 | 0.5918 | 109.375 | 0.0137 |
| 8 | 4812.500 | 0.6015 | 93.750 | 0.0117 |
| 0 | 5609.375 | 0.7011 | 2953.125 | 0.3691 |

This confirms removable polling work even with faster arithmetic. It does
not account for per-frame critical paths or establish the timer as a solution
to inconsistent pacing. Private evidence: `local/native-spu-float-timer*`
windows, reports, process logs, presents, thread CPU JSON and owned run files.

## Confirmed native arithmetic call overhead

The separate prior instruction profile named `canonical`, `flush` and
`validate_result` among hot decoder locations. Production object disassembly
then confirms three canonicalization calls, one flush call and an unconditional
validation call on the native FMA path. Calls cross a target-specific AVX-512
boundary, spill vectors and emit `vzeroupper`. Actual RelWithDebInfo flags include
`/O2 /Ob1` and `-O2`; small helpers lacking explicit inline annotations remain
out of line.

Canonicalization and flushing are now explicitly always-inlined. The audit
call is guarded inside each kernel, so validation-disabled execution returns
the result directly. Extended-range guards, scalar fallbacks, MXCSR behavior,
CPU/OS checks and failure latching are unchanged. Object disassembly confirms
canonical/flush calls disappear and the audit call is conditional.

The standalone oracle now uses flags matching the runtime's inlining and
floating-point contract settings. Before/after both pass **73,679,652 lane
comparisons**, across edge cases, random data and host FP modes, plus the
deliberate audit failure latch. Dependent changing-input FMA costs about
73–76 cycles/vector before versus 38–40 after. Scalar samples vary 83–103;
these local CPU costs do not establish an end-to-end FPS multiplier.
The earlier standalone measurements used `/O2` without explicitly matching
`/Ob1`, so they were not a precise model of production helper-call costs.

The full `-SpuFma -NativeSpuFloat -Test` build passes. Candidate SHA256 is
`F413223CDBD957385633537DB4B2ECF69AE19717324170528D4BB70768A61047`.
Logs: `local/native-spu-float-{before,after}-inline-oracle.log`,
`local/native-spu-float-{before,after}-asm.log` and
`local/native-spu-float-inline-build-tests.log`.

Muted audit run `20261012-223325-626` starts with arithmetic off. Capture
012457 establishes the carrier, then arithmetic is enabled with validation.
Capture 012983 confirms continued carrier gameplay. Normal close returns 0
with **2,716,946,847 vector audits and zero failures**. Audit mode publishes
scalar results; this is numerical evidence for that gameplay interval, not
a visual correctness, audio quality or whole-game completion claim.

## Ordinary present comparisons and delivery

Run `20261012-223622-974` uses that exact ordinary candidate, the same private
carrier fixture and rendering settings, native waits/cursor/texture tracking on,
and timer mode 0. Capture 011532 verifies the carrier before the windows. The
first order uses five seconds of warmup and 40 seconds per block; the reverse
uses five seconds and 30 seconds. Normal close returns 0 and flushes complete
consecutive present intervals. Arithmetic mode events validate every block.

| Order | Native | FPS | Mean ms | p95 ms | p99 ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| First | 0 | 14.391 | 69.490 | 97.843 | 107.210 |
| First | 1 | 18.965 | 52.727 | 77.292 | 98.036 |
| First | 1 | 19.815 | 50.466 | 72.549 | 94.670 |
| First | 0 | 17.377 | 57.548 | 79.330 | 89.279 |
| Reverse | 1 | 20.795 | 48.088 | 67.514 | 79.068 |
| Reverse | 0 | 17.614 | 56.774 | 90.385 | 123.257 |
| Reverse | 0 | 17.006 | 58.803 | 86.395 | 97.819 |
| Reverse | 1 | 23.045 | 43.393 | 64.150 | 80.988 |

Both orders favor native arithmetic, but these are **contended, drifting
measurements**, not a controlled estimate of its gain. Another Haze from
`.worktrees/input-prompts` overlaps the first order, so its process observation
check rejects isolation. Only the owned Haze is observed during the reverse,
with gaps below 2.15 seconds, but independent compiler processes were also
observed during this run. A limited eight-second process-counter snapshot
misses short-lived compilers and cannot quantify total contention. Do not
attribute cross-launch or before/after-inlining FPS differences to the kernel.
Native p95 remains 64-77 ms here; consistent pacing is unresolved.

Private records: `local/native-spu-float-inline-ordinary{,-reverse}-report.json`,
their window/mode/process/present records, and compiler-overlap JSON.

The user-test package is `local/releases/haze-wip-2026-10-12-performance`.
Its single launcher enables the already tested native paths automatically,
uses normal interactive saves/input/audio, 1440p, native shadow resolution,
VSync on and cap 0. Its exact binary is the SHA256 above. Native shadows differ
from the benchmark's `follow` setting, so those frame times are not a prediction
for the package. Diagnostic controls are internal; users need no opt-in steps.
The package is a WIP and makes no stable-framerate or graphics-correctness claim.

The follow-up executable also initializes these performance defaults itself;
the Haze build enables CPU-dispatched native arithmetic by default. Explicit
environment overrides remain available for internal comparisons. Registering
a healthy native texture tracker enables regional reuse without requiring a
private CPU-work control file. The generic SDK build default is unchanged.
The global timer experiment remains disabled because its FPS evidence is mixed.

## Executable defaults and remaining CPU leads

`scripts/build.ps1 -SpuFma -Test -OutputDirectory local/candidates/native-defaults`
passes the full regression gate without requesting `-NativeSpuFloat`.
The resulting SHA256 is
`E0A2706466E4880453279E3BEB5E096ED9797CBF3AD48E8BCFDA0C3E447A50FB`.
Muted run `20261012-225254-877` omits the native-path environment settings and
the CPU-work control file. Startup events verify arithmetic, cursor, native
FIFO/queue waits and idle parking; complete present intervals verify flags
5379, including automatically enabled regional tracking. Viewed captures
006341 and 010224 verify the carrier before/after. Normal close returns 0.
The tracker remains healthy with 1,546,830 skips and zero failures; arithmetic
remains enabled with validation off and zero failures. These ordinary counters
are not a fresh numerical oracle or proof of broad gameplay correctness.

A separate 30-second ordinary window, after five seconds of settling, records
549 complete intervals: **18.356 FPS, mean 54.478 ms, p95 77.195 ms,
p99 98.079 ms, max 110.625 ms**. The user's interactive test build overlaps this
window and is left untouched. These are contended absolute results, not a
before/after performance comparison. Captures, builds and CPU suspension are
outside the FPS window. Private evidence: `local/native-defaults-{windows,report}.json`,
the process observer, presents, run logs and `native-defaults-build-tests.log`.
The already delivered user package remains unchanged while it is being tested.

Two separate CPU diagnostics use this executable with the same settings.
The 12-second broad sample records main-thread CPU 7,515.625 ms, renderer
5,375 ms, decoder 5,375 ms and audio producer 4,531.250 ms. About 24,328 ms
of process CPU is missing from retained short-lived thread handles. A focused
16-second main/renderer sample records 8,203.125 / 6,859.375 CPU ms. Its 6,202
suspensions total 5,911 ms, so it is strongly perturbing and cannot quantify
ordinary FPS or additive per-frame costs. RIP/stack counts include waits and
are not CPU utilization percentages.

The main thread repeatedly reaches native FIFO waits and genuine indexed
job-completion sleeps, plus `vm_read32`, `vm_write32`, `vm_write64`, byte loads
and reservation checks. The renderer still reaches vertex remapping/packing,
texture/program hashing and real fence waits. Source inspection identifies a
specific removable diagnostic cost: normal byte/halfword reads on Windows
still enter hot-read/watch/histogram code; only their WebAssembly paths bypass
it when probes are disabled. Wider normal reads already have such a bypass.
Investigate this and the external call boundary for hot PPU memory operations,
preserving MMIO, diagnostic overrides and real publication/coherency behavior.
This is a measured lead, not an implemented or proven FPS improvement.

The focused report's top 50 recorded chains are dominated by several distinct
waiting call stacks, hiding rarer active callers despite their RIP samples.
`profile_native_cpu.py --stack-limit` now permits retaining more call chains
independently of raw instruction limits. The next targeted sample should retain
those callers before selecting a whole-function native replacement. Private
profiles: `local/native-defaults-cpu.json` and
`local/native-defaults-main-renderer-cpu.json` (UTF-16).
