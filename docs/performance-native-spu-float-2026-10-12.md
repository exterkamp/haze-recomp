# Native hardware SPU arithmetic experiment — 2026-10-12

The experiment remains on `experiment/native-cpu-kernels` and SDK
`experiment/haze-native-kernels`, rebased onto main `a872469` and haze `9d282dd`.
The objective remains consistent frame pacing and unlocked framerate with a
VSync option; an arithmetic microbenchmark cannot establish that objective.

## Profile before implementation

The prior 800 µs audio ring polling lead is already covered by the
[audio wait experiment](performance-audio-waits-2026-10-12.md): parking that
poll reduced CPU consumption but did not improve carrier FPS/pacing. Repeating
that change without new evidence would not address the measured bottleneck.

Instead, muted run `20261012-214059-598` uses the current rebased ordinary
candidate, SHA256 `38AA2D1A43DA9F8A789912A47BB71FCDE3E7F10A6353D33F83BB521A1AF8E5C0`,
with private carrier save/input, 2560×1440 rendering, 1280×720 window, VSync on
and frame cap 0. Native cursor/FIFO/queue/idle paths and regional texture tracking
remain on; vertex workers and both geometry caches remain off. Viewed captures
010260/012085 establish the carrier before/after; known visual issues remain.
Normal close returns exit 0. Job timing is enabled, so this is diagnostic evidence.

A 12.040-second dynamic thread sample uses 3 ms randomized intervals, scans
every two rounds and unwinds every four. It records 1,412 ms of suspension
overhead. Process CPU is 66,656 ms, tracked thread CPU 28,234 ms; short-lived
jobs account for substantial missing coverage (1,283 observed IDs, 31 opened
lifetimes, 1,778 failed opens after exit). RIP counts include waits, are not CPU
percentages, and its FPS must not be used as an ordinary-build benchmark.

The busiest opened SPU thread, host TID 54976, consumes 11,172 CPU ms. It has
146 RIP samples in `spu_float_sum`, with additional samples in `spu_fma`,
`spu_fm`, `spu_fa` and `spu_fs`. Unwound chains traverse image 27's arithmetic
and the firmware ATRAC decoder. This is real arithmetic work following the
recent corrected extended-range numerical semantics, not just idle polling.
The main producer still reaches real job-completion and FIFO publication waits.

A separate settled 20.052-second completed-job interval excludes instruction
sampling. Image 40 completes 382 jobs with mean wall time 27.700 ms; image 42
completes 958 with mean wall time 10.328 ms. Image 30 accounts for 25,661 short
completed jobs and the largest summed completed-job cycles. Persistent audio
tasks do not exit in this interval and are absent from that completed-job table.
Logging and workload differences prevent comparing these numbers directly with
the earlier pre-rebase job timings as an attributable regression percentage.

Private evidence: `local/rebased-spu-profile-cpu.json`,
`local/rebased-spu-profile-job-{window,summary}.json`, owned run metadata,
job log, captures and normal-close result.

## Native operation and compatibility

`runtime/spu/spu_float_native.c` computes ordinary four-lane additions,
multiplications and fused multiply-adds with AVX-512 embedded round-toward-zero
and suppressed exceptions. It does not change the host MXCSR. Exponent-zero
inputs/results become positive zero as required by the corrected arithmetic.
Extended-range and overflow-risk lanes retain the scalar integer algorithm.
Subtraction and signed multiply-add variants apply sign bits before dispatch.

Addition allows operand exponents at most 253, so their sum can remain within
the host finite range. Multiplication and FMA conservatively require exponent
sums at most 379; FMA also limits its addend exponent to 253. Exceptional lanes
are replaced individually. These guards deliberately retain some scalar work
rather than letting host infinity/NaN behavior replace extended finite values.

The compiler's AVX-512 target attribute is confined to the three kernels.
Ordinary runtime/generated code retains its existing instruction-set settings.
CPU AVX-512F and OS XCR0 vector-state support are checked before activation.
Unsupported platforms retain scalar arithmetic. Native helpers accept values,
not guest execution contexts, local-store mappings, instruction dispatch, or
memory-publication state.

Build explicitly with `scripts/build.ps1 -SpuFma -NativeSpuFloat`. The SDK CMake
option `PS3RECOMP_NATIVE_FLOAT_RUNTIME` defaults off, preserving ordinary builds.
Even in an enabled build, `SPU_FLOAT_NATIVE=1` opts in at runtime; its default
is zero. `SPU_FLOAT_NATIVE_FILE` selects live 0/1 changes, checked from the frame
pump at most every 100 ms. Release/acquire publication makes initialized settings
visible to job threads. Mode events carry native QPC timestamps.

`SPU_FLOAT_NATIVE_VALIDATE=1` compares every computed vector with scalar
arithmetic, counts audits/failures, and publishes the scalar result. A mismatch
disables the fast path permanently for that process. Mode control cannot revive
the failed state. Audit counters add synchronization and duplicate work, so audit
runs cannot support speed claims.

## Independent checks and preliminary cost

The standalone hardware prototype passes 24,559,872 lane comparisons against the
corrected scalar algorithm across eight host rounding/FTZ/DAZ modes, exhaustive
triples of 18 edge encodings, and random bit patterns. The production oracle
extends this to all six helper dispatches, enabled/disabled selection, exact
MXCSR preservation, and deliberate audit corruption/fail-stop behavior:
**73,679,652 comparisons pass** on this machine with supported CPU/OS state.
The mismatch log in the deliberate corruption test is expected, not a gameplay
failure. Unsupported CPUs explicitly report that hardware checks were skipped.

Changing-input dependent FMA tests consume all four output lanes. The prototype
takes about 44–46 cycles/vector versus 82–98 scalar; the production path with
its audit-selection branch takes about 52–54 versus 80–90. Constant-coefficient
fixtures and an earlier fixture consuming only one final lane were also
retained privately, but do not substitute for the changing-input comparison.
These are local CPU costs, not an FPS multiplier or critical-path attribution.
Those initial standalone figures use `/O2` without explicitly matching the
runtime's `/Ob1`; the [follow-up](performance-native-spu-float-followup-2026-10-12.md)
checks the actual helper calls and aligns the standalone compiler flags.

The full `-SpuFma -NativeSpuFloat -Test` build passes, including the existing
numeric, renderer, memory lifetime, DMA and queue checks. The ordinary candidate
SHA256 is `B39A8292FAD3531A6F51973C7D12CDA83615015BE814876D32CFC0C5F7BB5B8F`.
The final build log is `local/native-spu-float-final-build-tests.log`.

Muted audit run `20261012-215949-795` completes 2,824,305,356 vector audits with
zero failures, but its viewed capture is still loading; it is boot/loading
evidence only. A fresh audit run `20261012-220450-813` starts with native
arithmetic off and enables it after capture 011339 establishes the carrier.
Capture 011912 confirms that carrier gameplay continues with validation on.
It closes normally with **2,185,795,503 vector audits, zero failures**, and no
failed latch. Audit mode publishes scalar results throughout. These captures
do not establish visual correctness, broad mission coverage or audio quality.

## Ordinary successful-present comparisons

Run `20261012-220733-494`, owned PID 58524, uses the same nonprofiling candidate
and scene settings as above, with validation and job timing off. Capture 011150
establishes the carrier before recording; capture 023014 shows the same viewpoint
afterward. Only the owned Haze is observed in both sets of bracketed process
samples, with maximum gaps below 2.17 seconds. This does not exclude other
applications or processes that start and exit between samples.

Each block has five seconds of warmup. The first order measures 45 seconds per
block and the reverse order 30 seconds. Captures, builds and instruction sampling
are excluded from these windows. Process-wide native mode events validate every
block. Normal close flushes the latest 16,384 successful presents, covering all
eight measured blocks, and returns exit 0.

| Order | Native | FPS | Mean frame ms | p95 ms | Mean CPU cores |
| --- | ---: | ---: | ---: | ---: | ---: |
| First | 0 | 26.607 | 37.584 | 54.308 | 6.282 |
| First | 1 | 26.882 | 37.200 | 52.588 | 5.687 |
| First | 1 | 27.152 | 36.829 | 52.067 | 5.740 |
| First | 0 | 26.564 | 37.646 | 52.055 | 6.638 |
| Reverse | 1 | 26.335 | 37.973 | 53.249 | 5.692 |
| Reverse | 0 | 24.093 | 41.506 | 58.868 | 6.072 |
| Reverse | 0 | 23.176 | 43.148 | 61.087 | 6.233 |
| Reverse | 1 | 27.608 | 36.221 | 51.362 | 6.055 |

The first order's block-average frame time improves 37.615 to 37.015 ms
(1.59%); the reverse improves 42.327 to 37.097 ms (12.36%). The variation is too
large to promise a stable percentage. Process CPU per presented frame improves
243.28 to 211.71 ms in the first order and 260.70 to 218.21 ms in the reverse.
Both orders favor the native path, but p95 remains roughly 51–61 ms and the
carrier remains below a consistent 30 FPS. This does not establish the 144 FPS
target, broad mission coverage, or a comparison against a build with the feature
compiled out. Keep build and runtime activation opt-in.

Private records are `local/native-spu-float-ordinary{,-reverse}-{windows,report}.json`,
their process-observation logs, the owned run metadata, present CSV and mode log.

## Separate CPU follow-up and next lead

After both present comparisons, separate 12-second dynamic thread samples use
the same 3 ms randomized intervals and stack settings as the initial profile.
The same persistent decoder host TID 54668 consumes 6,547 CPU ms with native
arithmetic versus 9,969 with scalar arithmetic. Its samples move from
`spu_float_sum` to native add/multiply/madd, their canonicalization helpers and
more task-signal waits. The main producer TID 2756 stays near 8,922/8,938 CPU ms;
renderer TID 43840 is 6,234/5,672 ms. These are sequential diagnostic intervals,
not per-frame critical-path accounting.

The audio PPU worker TID 9696 consumes 5,016 ms with native arithmetic versus
1,625 scalar. Its stacks include the 800 µs ring polling path through
`func_00B73738` and `sys_timer_usleep`. This supports investigating whether
faster decode exposes more polling, but does not prove that relationship.
The previously investigated audio wait option should be compared together with
the arithmetic path before attributing a gain to that combination. The producer's
real completion waits and short-lived job work also remain substantial.

Each profile suspends threads for about 1,313 ms in total. Process CPU is
69,484/74,281 ms, but 41,078/46,219 ms is not covered by opened thread lifetimes.
Do not turn RIP counts into CPU percentages or use these sampled intervals as
FPS measurements. Private evidence: `local/native-spu-float-cpu-{1,0}.json`.

## Reproduction

```powershell
./scripts/build.ps1 -SpuFma -NativeSpuFloat -Test -OutputDirectory local/candidates/native-spu-float
```

Use a private checkpoint and muted audio with normal device timing. Validate
with `SPU_FLOAT_NATIVE_VALIDATE=1` separately, then launch the same ordinary
binary with validation off for 0/1/1/0 and reversed successful-present blocks.
`benchmark_present_modes.py --mode-column spu_float_native` checks process-wide
mode events. Observe other Haze processes read-only; do not stop other agents'
games. Keep captures, builds, job logging and CPU sampling outside FPS windows.
