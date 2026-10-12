# Measured SINT16 batch specialization

SDK commit `82132e9` on `experiment/haze-native-kernels` adds size-specialized
signed-16-bit vertex loops for one, two and three components. They preserve
exact-width reads, signed conversion, big-endian input and missing `(0,0,0,1)`
components. Four-component SINT16 retains the previous decoder after a paired
benchmark found no benefit from its proposed replacement.

## Evidence and rejected alternative

The separate native-FIFO-on ordinary diagnostic `20261012-183812-351` sampled
renderer TID 10708 for 12 seconds. Its 96 samples in `rsx_vertex_fetch_many_ex`
include 17 at `+0x986` (the per-vertex size dispatch), nine at `+0x960` (address
calculation), and samples in the SINT16 lane conversion/stores at `+0x931`,
`+0x936` and `+0x939`. The matching optimized object shows size dispatch within
the old vertex loop, with repeated preferred-lane/default handling. These are
instruction locations, not CPU percentages. The profile includes waits and
suspension overhead; see the earlier VP-hash report for its limits.

The same object maps `+0x726` to normalized byte-color conversion followed by
vector division. A synthetic prototype first tested exact 256-entry lookup
and double-reciprocal multiplication against the current divide. All outputs
matched the reference, but median costs across its synthetic batches were
2.292, 4.927 and 2.594 CPU cycles/vertex respectively. The lookup was slower;
neither alternative was adopted. Raw prototype/data remain local in
`local/bench_unorm8_candidates.c` and `local/bench-unorm8-candidates.txt`.

## Actual API benchmark and correctness

`tests/benchmark_vertex_fetch.c` calls the production batch API, checks its
complete output against the reference batch before and after timing, and uses
QueryThreadCycleTime. It covers sizes 1..4, 1/2/4/8 attributes, 64/256/1024
vertices, permuted references and unaligned source pointers. Three rounds time
2,000 calls per case. This is a host-kernel diagnostic, not a prediction of FPS.

The final same-binary sequence selects modes 1/3/3/1 (original SINT16 versus
size-specialized SINT16, with previous F32/byte specialization retained).
Median cycles per vertex per attribute across each size's cases:

| SINT16 components | Original 1 | Native 1 | Native 2 | Original 2 |
| --- | ---: | ---: | ---: | ---: |
| 1 | 4.510 | 4.029 | 4.001 | 4.412 |
| 2 | 4.595 | 2.988 | 2.994 | 5.094 |
| 3 | 5.239 | 3.950 | 3.984 | 5.307 |
| 4, retained decoder | 3.867 | 3.928 | 3.879 | 3.859 |

The one/two/three-component savings repeat; the unchanged four-component path
is effectively flat. CPU cycles exclude descheduled time, but frequency/core
and data-layout variation still limit generalization. Earlier prototypes used
different code layouts and do not substitute for this final paired sequence.
Artifacts: `local/native-sint16-final-{1,2,3,4}.csv`.

Reproduce the current kernels with:

```powershell
./scripts/benchmark_vertex_fetch.ps1 -Tag original -Modes 1
./scripts/benchmark_vertex_fetch.ps1 -Tag native -Modes 3
```

The full ordinary-build regression suite passed. Every signed 16-bit value in
every supplied lane/component count is compared with the scalar decoder under
batch modes 1, 2 and 3. Protected source tails also cover mode 0. Existing F32/color
exhaustive cases, remapping, defaults, divisors, reused plans and actual graphics
readbacks also pass. Log: `local/native-sint16-final-build-tests.log`.

## Independent gameplay selection

The existing boolean `_ex` API retains its contract and selects all specialized
kernels when true. New `rsx_vertex_fetch_many_mode` separates F32/byte bit 1
from SINT16 bit 2 for diagnosis. Renderer CPU-work bit 2048 independently enables
SINT16 specialization. Normal renderer flags remain 1283; 3331 adds only this
new bit. The diagnostic tools accept flags through 4095, and their existing
present-mode rejection tests additionally cover 1283/3331 transitions.

The ordinary candidate is `local/candidates/native-sint16-batch/HazeRecomp.exe`,
SHA256 `C851CF94338D5D61B2679021A98AB6661766C68BC27C7AF9802CEF014A2ED0F7`.
The new renderer path remains opt-in pending ordinary gameplay measurements.
Stable carrier pacing and broad unlocked gameplay correctness remain unresolved.

## Ordinary-build gameplay comparison

Muted run `20261012-185807-186` used the final candidate, a fresh private carrier
checkpoint/input file, 2560x1440, AA off, AF1, follow-resolution shadows, VSync
on and no host cap. Native cursor, PPU queue, SPU idle and FIFO wakeups stayed
enabled throughout. Captures 008570/015101 verify the carrier before and after.
Four 45-second windows followed five-second warmups with no input, screenshots,
instruction sampling, builds or standalone benchmarks inside them. The game
closed normally with exit zero, preserving the complete present ring.

The reporter validated settled 1283/3331 flags and consecutive present intervals.
Its strict process-observation coverage check passed for all four windows:
only the owned Haze was observed, with two-second sampling. This does not
exclude unrelated applications or processes living entirely between samples.

| SINT16 mode | FPS | Mean ms | p95 ms | p99 ms | Max ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| Original | 25.084 | 39.866 | 55.556 | 62.370 | 318.676 |
| Specialized | 24.341 | 41.084 | 58.078 | 66.827 | 402.457 |
| Specialized | 25.261 | 39.587 | 56.599 | 67.118 | 112.708 |
| Original | 25.028 | 39.955 | 56.264 | 62.391 | 73.929 |

This does not establish an FPS or pacing improvement. The synthetic kernel
saving is real for its tested inputs, but its fraction of the frame and the
effect of live scene variation are not established by these results. Retain
renderer bit 2048 as opt-in. A separate light renderer diagnostic should measure
vertex-stage costs with native FIFO wakeups before another promotion decision;
do not turn the microbenchmark percentage into a whole-game claim.

Artifacts: `local/native-sint16-{run,windows,report}.json`, presents CSV and
process-observation JSONL.

## Separate texture-check lead

Current texture checks read the full source once per cached texture per frame.
The live host reserves its 4-GB arena in `port/main.cpp::alloc_guest_vm`, then
commits 64-KB regions on demand. Windows write tracking is a possible separate
native experiment: [VirtualAlloc](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualalloc)
requires `MEM_WRITE_WATCH` with reservation, and
[GetWriteWatch](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-getwritewatch)
can retrieve dirty page addresses and reset tracking in the same call. Microsoft's
documentation warns that separate query/reset calls can miss writes unless
writers are excluded between them.

This is a research lead, not an implemented or benchmarked optimization.
Allocation compatibility, tracking/reset overhead, partial pages, overlapping
textures, concurrent producers and writes between the frame boundary and first
binding need explicit validation. Skipping a hash without covering those writes
could conceal real texture mutation. No arena or texture-invalidation change
is included in this checkpoint.
