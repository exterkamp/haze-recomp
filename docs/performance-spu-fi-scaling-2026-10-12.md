# Native SPU FI scaling, 2026-10-12

## Profile lead and change

The image-40 samples in `local/drain-hook-outline-image40-samples.json` include
52 observations in `ldexp` out of 941 total. They are instruction-pointer
observations for one job image, not whole-process CPU percentages. The generated
image `spu_0039_at_00F1C100` calls `spu_fi` at many live arithmetic sites. Its
helper used two `ldexpf` calls per lane, including one with a constant exponent.

The SDK `spu_helpers.h` now uses native power-of-two multiplication for FI's
normal scale and constant interpolation denominator. For exponent fields
14..255, the scale `2^(exp-140)` is itself a normal float, and the unsigned
10-bit step fraction multiplies exactly without overflow or a nonzero
subnormal result. For smaller exponents the original `ldexpf` remains.
The unsigned 19-bit interpolation numerator times `2^-19` is also exact.
Base decoding, sign comparison and the final multiply/subtract are unchanged;
fast-math and reassociation remain disabled. This preserves the prior helper's
returned values; it is not a new claim of complete SPU hardware conformance.

## Numeric oracle and build gate

SDK `runtime/spu/tests/test_fi_scaling.c` retains the exact pre-change helper
as an independent reference. The oracle exercises all 256 exponent fields,
all 1024 step fields, sign/base boundaries, all 524288 interpolation numerator
values at representative step endpoints, and deterministic random full-bit
inputs. It runs under all four host rounding modes and all four FTZ/DAZ
combinations, checking returned bits including zero, infinity and NaN cases.

Both SSE4.1-only and AVX/FMA builds passed **104857600 exact lane comparisons**
each. The reference and optimized functions are separately noinline; inputs
are generated at runtime. The test restores the host FP environment afterward.
This oracle checks result bits rather than libc errno or exception-flag side
effects, which the existing guest helper did not expose as SPU state.

`scripts/build.ps1 -Test` includes the oracle using the selected SPU ISA flags
and the existing 17-case numeric/estimate-refinement test. The full regression
gate passed for the ordinary candidate, including existing renderer/GPU,
queue/coherency, input, audio, SPU and HLE tests:

```powershell
./scripts/build.ps1 -SpuFma -Test -OutputDirectory local/candidates/fi-scaling
```

Build log: `local/fi-scaling-build-tests.log`. Candidate executable SHA256:
`5BE1E8DBCF90721E92BAF57EB1B963925BD4401AD43992856FADE261B32D9E57`.
Renderer profiling is off, FMA on, byte comparison off.

## Controlled helper cost

The same source can compile a benchmark with `SPU_FI_BENCHMARK` defined. It
compares the unchanged reference and new helper in one process, pins only its
own benchmark thread to an allowed logical CPU, and alternates
old/new/new/old/new/old/old/new. Each block performs one million four-lane calls
on runtime-generated normal finite estimate inputs, exponent fields 80..180.
Both output lanes and checksum consumption remain live. Denormal and special
cases belong to the oracle rather than this typical-input cost measurement.

From a Visual Studio x64 developer shell with clang-cl available, at repo root:

```powershell
clang-cl /nologo /W0 /O2 /clang:-msse4.1 /clang:-mavx /clang:-mfma /DSPU_FI_BENCHMARK /Iexternal/ps3recomp/runtime/spu /Iexternal/ps3recomp/include /Felocal/benchmark_fi_scaling.exe /Folocal/benchmark_fi_scaling.obj external/ps3recomp/runtime/spu/tests/test_fi_scaling.c
local/benchmark_fi_scaling.exe
```

| Implementation | Cycles per four-lane call |
| --- | ---: |
| Original | 137.96, 122.59, 153.08, 107.89 |
| Native scaling | 25.29, 30.18, 25.51, 25.39 |

All windows used logical CPU 0 and returned checksum `8619039153979154`.
Raw evidence: `local/fi-scaling-cost.jsonl`. Game thread affinity was not
changed. This is a confirmed local helper-cost reduction, not a whole-game
FPS gain or proof of consistent pacing. Correct unlocked gameplay timing
remains outstanding. SDK implementation commit: `f9b9b7b` on the `haze` fork.

## Unprofiled gameplay and remaining pacing

Run `20261012-095549-270` used the candidate above, real audio muted,
2560x1440 rendering, follow-resolution shadows (5120-square atlas), AA off,
filtering 1, VSync auto and cap 30 in a 1280x720 window. Native PPU queue waits
and the opt-in SPU idle wait were both enabled from startup. The copied profile
and checkpoint loaded successfully. Only owned PID 24816 was observed throughout
the measured blocks; the two-second observer cannot exclude other applications
or processes shorter than its sampling interval.

Two stationary carrier blocks and one squad block each measured 60 seconds
after five seconds of settling. No input, screenshots, instruction sampling or
builds ran inside those windows. The report verifies consecutive present IDs,
the native SPU worker's mode events (host TID 52960) and process observation
coverage. Every complete interval, including stalls, is retained.

| Scene | Frames | FPS | Mean ms | p95 ms | p99 ms | Worst ms | Mean process cores |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Carrier 1 | 1434 | 23.91 | 41.82 | 56.15 | 63.05 | 83.68 | 4.01 |
| Carrier 2 | 1415 | 23.60 | 42.37 | 57.07 | 65.91 | 78.51 | 4.08 |
| Squad | 1562 | 26.04 | 38.40 | 51.22 | 59.11 | 243.76 | 4.23 |

Captures 8038 and 15931 verify the initial carrier view. Closed-loop bounded
input then approached the squad, with captures 20604 and 21529 showing the
close group and conversation camera; capture 24108 after the measured block
shows the changed camera and squad. The sustained 1–2 FPS collapse did not
recur in this attempt. This single successful traversal does not establish
that intermittent slowdown is fixed. The squad block still contains a
243.76-ms stall and 102 frames over 50 ms. Consistent 30 FPS is not achieved.

Raw evidence is local: `fi-scaling-presents.csv`,
`fi-scaling-carrier-windows.json`, `fi-scaling-carrier-report.json`,
`fi-scaling-squad-windows.json`, `fi-scaling-squad-report.json` and
`fi-scaling-processes.jsonl`. The process observation gap was at most 2.15 s.
The game closed normally with exit 0 at 10:11:56 UTC, flushing its present log.

These are current-build measurements, not an isolated before/after estimate
for FI. Previous carrier windows from the drain-hook build ranged from 20.52
to 24.01 FPS, overlapping these results. NPC and cross-session variation make
a whole-game improvement from FI unproven despite the confirmed helper cost.

A separate six-second CPU sample after the squad block is preserved as
`local/fi-scaling-squad-cpu.json`. Process CPU was 28312.5 ms, of which 13937.5 ms
was tracked and 14375 ms unattributed, including missed short-lived jobs.
Guest thread 8 (entry E77590, host TID 64624) used 5015.625 ms; its samples often
land in timer/delay services. Queue worker 48080 used 3984.375 ms and renderer
18896 used 3250 ms. These observations identify follow-up leads rather than
instruction-level CPU percentages or a demonstrated cause of the stalls.
The persistent idle worker is no longer the dominant tracked CPU consumer.
