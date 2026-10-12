# Native three-word ring-copy experiment

The refreshed carrier profile in `performance-guest-pacer-2026-10-12.md` found
runtime image 42 among the largest completed-job CPU consumers. Sampling that
image caught 57 of 442 instruction locations in its LS `35F8` loop. Those
samples include waits and are not a CPU-utilization percentage or proof of a
whole-frame bottleneck.

## Audited operation and native implementation

Image 42 is the locally lifted `spu_0041_at_00F2E380`. Its `35F8` loop copies
three aligned words per item from a source array into a 2048-entry ring. The
translated implementation repeatedly loads quadwords, rotates a source word
into place, builds shuffle masks, and rewrites destination quadwords.

The native prefix copies the first `count - 1` items directly, retaining the
original order of each individual four-byte read and write. This matters when
source and destination overlap: one large `memcpy` or `memmove` is not generally
equivalent. Addresses retain the original source-index, ring-index, 32-bit
integer, and 256 KB local-store wrapping behavior. The original final iteration
still executes, restoring every scratch register and nonpreferred vector lane
before the unchanged exit to `3690`. DMA channels, tag waits, task completion,
and dispatch are untouched.

Runtime guards require image 42, PC `35F8`, its own local store, non-policy mode,
aligned buffers, the audited masks, and a bounded remaining count of 2..2048.
Active or uninitialized LS debugging probes retain the translated path. The
build patches an ignored copy and checks both the complete image SHA256 and
the function hash, plus the image registration. Game-derived source stays local.

`HAZE_NATIVE_TRIPLET_COPY=1` enables the prefix. `HAZE_TRIPLET_COPY_FILE` can name
a 0/1 control file polled at most once per 100 ms across job threads. A shared,
atomic selector and process-wide mode events support same-process comparisons;
one first-use message confirms that the runtime guards actually accepted work.
The feature defaults off pending ordinary gameplay performance validation.

## Correctness and loop cost

The test extracts the actual audited original loop from the local generated
source and compiles it alongside the patched version. It compares the complete
SPU context, all registers, all local-store bytes, and the exit dispatch target,
normalizing only the host LS pointer. The 2,777 comparisons include randomized
nonpreferred lanes, 32-bit overflow, source-index and ring wrap, local-store
wrap, overlapping buffers, and translated fallback cases. Additional guards
check unchanged state for invalid counts, LS ownership and debugging probes.
Patch tests reject a changed image, a changed loop and double insertion.

The ordinary candidate built and passed the full regression suite. The shared
control selector was rebuilt afterward, with benchmark mode-evidence rejection
tests passing separately. Candidate SHA256:
`CB3555545FB184B45E29D8FAD29F0D2E8639CC7440DFE7524A8FB3B8B4737947`.

One 2,000-call microbenchmark with 1,024 items per call measured 28.247 us for
the translated loop and 1.207 us for the native prefix plus final translated
iteration, a 23.409x ratio. This is the loop's cost in the test fixture, not an
FPS multiplier, critical-path saving, or measurement of live game input sizes.

Artifacts: `local/native-triplet-build-tests.log`,
`local/native-triplet-control-build.log`, `local/triplet_oracle.c` (private,
extracted game-derived test fixture), and `local/test_spu_triplet_copy.exe`.

## Ordinary-build comparison

Muted ordinary run `20261012-151010-710` used 2560x1440, follow-resolution
shadows, AA off, AF1, host cap zero, VSync off, native PPU queue waiting and SPU
idle waiting. Capture 93690 verifies the carrier camera before recording.
Four 30-second blocks with five-second warmups switched native copy
original/native/native/original, while guest pacing bypass stayed enabled.
The first accepted prefix logged six native items, followed by its original
final iteration. Native mode events validate the switches.

Another Haze instance from `.worktrees/shadow-lighting` started after this run's
launch and is observed throughout the measurement windows. The strict report
therefore rejects the session as an isolated benchmark. Diagnostic reports
without isolation validation are retained explicitly as *contended*:

| Native-copy block | FPS (contended) | p95, ms | Mean CPU cores |
| --- | ---: | ---: | ---: |
| Original 1 | 20.223 | 68.312 | 4.146 |
| Native 1 | 19.245 | 71.678 | 3.983 |
| Native 2 | 19.827 | 71.504 | 3.995 |
| Original 2 | 20.213 | 67.410 | 4.127 |

This does not demonstrate an FPS or pacing improvement. Lower observed CPU
use also cannot be attributed exclusively to the copy under shared contention.
An isolated comparison and current image-42 phase profiling are still needed
before deciding whether to enable the feature by default.

A second four-block experiment kept native copy enabled and switched the guest
pacer bypass 0/1/1/0. Contended FPS was 19.981 / 19.103 / 19.479 / 19.083, with
p95 69.743 / 71.696 / 70.251 / 71.097 ms. It is likewise excluded from isolated
performance claims. Both experiments retained complete CSV coverage, and the
measurement command automatically requested normal close afterward. The game
exited normally with code zero, preserving the recorder contents.

Artifacts: `local/native-triplet-abba-{windows.json,presents.csv,processes.jsonl}`,
`local/native-triplet-contended-report.json`,
`local/native-triplet-pacer-{windows,contended-report}.json`.

Consistent pacing, broad higher-rate gameplay timing, and VSync-on verification
remain outstanding; the full goal is active.
