# Coherent row copies for scaled-image blits

Stable 30 FPS remains unverified. This follows the
[SPU byte-operation experiment](performance-spu-bytes-2026-10-11.md).
All native runs were muted, with the real audio pipeline retained.

## Evidence and change

Normal-build run `20261011-235534-845` reached the stationary carrier
checkpoint (capture 9361). A 20-second whole-process instruction sample
identified the rendering thread. A separate focused sample collected 9,701
locations from that thread over 20.01 seconds. Its largest active groups
included texture hashing (582), vertex fetching (558), remapping (357),
NV3089 blits (350), and shader hashing (328). Waiting-thread samples are not
CPU utilization percentages. An earlier interval starting during loading is
excluded from these conclusions.

Native assembly located the NV3089 samples inside its linear per-pixel loop,
which repeats clipping, pixel-address arithmetic, store-watch checks, and SPU
reservation checks for every pixel. The new path clips once, uses the same
fixed-point point-sampling formula, and publishes each row through the existing
coherent block writer. Unit horizontal scale copies a contiguous source row;
other scales gather exact-width pixels into a reusable row buffer. The GPU
mirror/resolve still executes, and guest image bytes are still copied.

Whole source/destination ranges must be disjoint and fit the 32-bit address
space. Overlap, PPU reservation tracking, active write watches, allocation
failure, and unsupported inputs retain the original scalar path. Swizzled
destinations keep their existing implementation. The block writer serializes
and raises reservation-loss events for touched SPU reservation lines.

The regression compares 8,192 clipped/fractional/scaled images of 1-, 2-, and
4-byte pixels against an independent pixel loop, checks both sides of a
reservation-line boundary, verifies non-mutating fallback conditions, exercises
the actual NV3089 trigger, and uses inaccessible pages to detect overreads and
overwrites at exact pixel tails. It and the existing complete suite passed.

## Controlled carrier comparison

Run `20261012-000729-657` was visually checked at the carrier checkpoint
(capture 7681). Each block contains 180 consecutive presents after 80 warmup
frames. Both modes use heap SPU contexts, SIMD SPU byte operations, bulk vertex
references, and dense remapping. Shader cache is warm; `RSX_PROFILE_LIGHT=1`.
No builds, captures, instruction sampling, or job logging occurred during the
blocks. Blit timers are identical in both modes and include the whole NV3089
operation; their scope is separate from draw preparation.

| Copy path | Frames | FPS | p95 ms | Blit CPU ms | Draw CPU ms | Mean draws | Mean CPU cores* |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Pixel loop | 8451-8630 | 22.24 | 59.68 | 2.434 | 24.65 | 1458 | 5.02 |
| Rows | 8721-8900 | 23.67 | 54.82 | 0.327 | 23.27 | 1444 | 4.99 |
| Pixel loop | 9021-9200 | 24.40 | 53.70 | 2.469 | 21.38 | 1241 | 5.13 |
| Rows | 9321-9500 | 24.50 | 53.27 | 0.325 | 21.85 | 1337 | 5.25 |
| Pixel loop | 9621-9800 | 22.90 | 56.14 | 2.486 | 24.06 | 1378 | 5.33 |
| Rows | 9921-10100 | 23.55 | 56.77 | 0.329 | 23.76 | 1383 | 4.99 |

*CPU counters cover propagation, warmup, and measurement, with different
bounds from the frame interval. NPC work and draw counts vary.

The stage saving of about 2.1 ms repeats in every pair. FPS favors rows in
each pair, but the entire FPS difference cannot be assigned to this change.
The third pair's p95 is slightly worse, so consistent pacing is not established.
Rows become the default; this is still below the 30 FPS requirement.
Capture 10823 checks the optimized scene after the blocks. The run closed
normally with exit code 0.

## Reproduction

The recorded comparison included the live SPU comparison code. Reproduce it
with `scripts/build.ps1 -SpuFma -ProfileRenderer -CompareSpuByteOps -Test`. Set
`RSX_PROFILE_LIGHT=1` and `RSX_PROFILE_CSV=renderer.csv`. Point
`RSX_CPU_WORK_FILE`, `SPU_EXECUTION_MODE_FILE`, and `GCM_ROW_BLIT_MODE_FILE`
at the same ignored file, initially containing 99. Verify gameplay, then:

```powershell
.venv/Scripts/python.exe scripts/benchmark_cpu_work.py <renderer.csv> <control.txt> --flags 99 227 99 227 99 227 --pid <HazePID> --output <blocks.json>
```

Bit 128 selects row copies. The driver restores zero afterward; remove all
diagnostic controls for ordinary play. New CSV fields are `blit_cpu_ms`,
`blit_requested_pixels`, and `blit_row_pixels`. Pixel counters count requested
areas, including clipped pixels, rather than claiming all pixels were written.

For focused instruction sampling, use `scripts/profile_native_cpu.py <PID>
20 --tid <render-thread-ID> --interval-ms 1 --raw-limit 100`, separately from
FPS benchmarks. Process ownership is still verified before suspending a thread.
All game assets, generated translations, raw logs, and captures stay local.
