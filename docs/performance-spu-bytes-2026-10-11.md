# Targeted SPU byte-operation experiment

The stable 30 FPS goal remains open. This follows the
[SPU storage measurements](performance-spu-storage-2026-10-11.md). Every native
run in this report was muted. No jobs or simulation steps were skipped.

## Profile and implementation

The image-40 sampler collected 1,990 instruction samples from 369 jobs over
20 seconds in run `20261011-233652-929`. Compiled functions `0x3450` and
`0x35B4` accounted for 572 and 219 samples. Inspection of their generated C
and native assembly found scalar quadword byte gathers and shifts, including
zero-distance shifts compiled into byte extraction and reconstruction.
These are sample counts for one recurring job, not whole-process CPU shares.

Nine rotate/shift helpers now share SSSE3 implementations. Their shuffle
indices account for the SPU byte order inside host-native words. Zero-distance
operations return the input directly, and shifts beyond the quadword return
zero. The scalar implementation remains the fallback on other targets.
An independent byte-position oracle verifies 2,359,296 scalar/SIMD cases,
covering every relevant count, negative immediates, ignored high control bits,
poisoned unused lanes, and random input vectors. The full regression suite also
passed in both the profiling build and the normal build.
The normal build also reached the carrier scene in muted smoke run
`20261011-235311-619` (capture 10418), then closed normally with exit code 0.

## Controlled in-game result

Run `20261011-234750-547` was visually checked at the stationary carrier
checkpoint (capture 9263). Each block contains 180 consecutive presents after
80 warmup frames. Both modes use heap contexts, bulk reference construction,
and dense remapping. There were no builds, instruction sampling, job timing,
or captures during the blocks. Shader cache was warm and
`RSX_PROFILE_LIGHT=1`.

| Byte operations | Frames | FPS | p95 ms | Draw CPU ms | Mean draws | Mean CPU cores* |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| Scalar | 9608-9787 | 20.79 | 63.05 | 26.18 | 1455 | 5.23 |
| SIMD | 9921-10100 | 21.22 | 63.20 | 25.26 | 1336 | 4.93 |
| Scalar | 10221-10400 | 22.51 | 58.64 | 23.85 | 1280 | 5.36 |
| SIMD | 10521-10700 | 21.01 | 65.20 | 26.14 | 1368 | 5.16 |
| Scalar | 10821-11000 | 19.52 | 66.16 | 27.38 | 1392 | 5.21 |
| SIMD | 11121-11300 | 19.62 | 69.20 | 27.41 | 1420 | 5.15 |

*Process CPU counters include propagation and warmup, so their bounds differ
from the measured frame interval. NPC activity and draw counts vary.

CPU use fell in all three pairs, but FPS did not improve consistently and
p95 was worse in each pair. This experiment does **not** demonstrate an FPS
or pacing improvement. The SIMD path is retained for its lower measured CPU
use and equivalent semantics; stable 30 FPS and stutter remain unresolved.
The run closed normally with exit code 0 and no FIFO recovery or coherency-leak
messages.

The live comparison is compiled only for profiling builds. Each newly
dispatched asynchronous job captures its mode, which stays fixed for that
job's lifetime; persistent workers do not switch midway through a job.
Normal builds use SIMD without the comparison TLS reads or branches.

## Reproduction

Build with `scripts/build.ps1 -SpuFma -ProfileRenderer -CompareSpuByteOps -Test`. Set
`RSX_PROFILE_LIGHT=1` and `RSX_PROFILE_CSV=renderer.csv`. Point
`RSX_CPU_WORK_FILE` and `SPU_EXECUTION_MODE_FILE` to the same ignored control
file, initially containing 35. Verify the carrier checkpoint, then run:

```powershell
.venv/Scripts/python.exe scripts/benchmark_cpu_work.py <renderer.csv> <control.txt> --flags 35 99 35 99 35 99 --pid <HazePID> --output <blocks.json>
```

Bit 32 selects heap contexts; bit 64 selects SIMD in profiling builds. The
benchmark restores zero afterward. Remove diagnostic controls for ordinary
play. Assets, translated game code, captures and raw logs remain local.

The next measurement should re-sample the longest recurring job with SIMD
enabled and inspect the renderer's remaining 24-27 ms of draw preparation.
The older profile also found `ldexp` and transfer checks; neither has been
changed on the strength of this experiment.
