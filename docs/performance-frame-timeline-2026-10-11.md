# Frame timeline and representative profiling builds — 2026-10-11

These measurements predate the 30 FPS limiter. Use `scripts/run.ps1 -Uncapped`
for comparable current-build throughput diagnostics.

Stable 30 FPS remains unverified. These measurements identify work to investigate;
they do not establish another optimization win.

## Measurement changes

Renderer CSV now includes `qpc_frequency`, `draw_begin_qpc`, and `draw_end_qpc`.
The draw boundaries reuse the existing profiling timestamps. They locate the
first and last draw preparation calls relative to consecutive frame timestamps.
The span between them can include other work and scheduling waits; subtracting
summed draw CPU scopes does not identify the cause of that remaining time.
All additions are restricted to profiling builds.

Live scalar/SIMD SPU comparisons are now an explicit build experiment:
`scripts/build.ps1 -SpuFma -ProfileRenderer -CompareSpuByteOps -Test`.
Ordinary `-ProfileRenderer` builds use the same SIMD-only byte helpers as normal
play. The comparison branches can change inlining, so their presence should be
reported with benchmark results. The earlier byte-operation and row-blit A/B
reports now document this setting. Their paired comparisons remain useful, but
they do not substitute for measurements of the ordinary executable.

## Separate SPU job diagnostic

Muted run `20261012-001349-558`, visually verified carrier scene, frames
8750–9049, live comparison code compiled, row copies enabled. Job timing was
enabled only for SPU image 40. No instruction sampling ran during this interval.

- 300 image-40 jobs completed, one per frame.
- Job wall time: mean 17.293 ms, median 17.194 ms, p95 20.191 ms.
- Before first draw: mean 27.797 ms.
- First-to-last draw span: mean 31.498 ms; summed draw preparation: 23.713 ms.
- After last draw: mean 0.772 ms.

Jobs can overlap the draw span. These boundaries do not prove that image 40
causes the delay before the first draw. Job wall times use QPC, rather than the
coarser printed millisecond counters. The job-logging run is not an FPS benchmark.

Reproduce the offline analysis with:

```powershell
.venv/Scripts/python.exe scripts/summarize_frame_jobs.py <renderer.csv> <boot.stderr.log> --start-frame 8750 --frames 300 --image 40 --output <report.json>
```

Use `SPU_JOB_TIMING=1` and `SPU_JOB_TIMING_IMAGE=40` only in a separate diagnostic
run. Persistent jobs that do not return are absent from the completion log.

## Unlogged profiling run using normal SPU helpers

Muted run `20261012-002412-747`, carrier verified in capture 8271, consecutive
frames 8500–8799. Full regression suite passed before launch. Build used
`-SpuFma -ProfileRenderer -Test`, with `HAZE_SPU_BYTE_COMPARE=0` confirmed and
no `SPU_COMPARE_BYTE_OPS` definitions in the generated build file. Heap SPU
contexts, SIMD byte helpers, and coherent row copies were enabled by default.
No job logging, instruction sampling, builds, or captures ran during the window.

| Measurement | Value |
| --- | ---: |
| FPS | 19.553 |
| Mean frame time | 51.144 ms |
| p95 frame time | 68.872 ms |
| Frames above 33.333 ms | 296 / 300 |
| Draw preparation | 27.738 ms |
| Vertex fetch/pack | 9.417 ms |
| Texture hashing | 3.619 ms |
| PSO preparation | 3.953 ms |
| Texture decode | 2.200 ms |
| Coherent blit CPU | 0.327 ms |
| GPU | 1.537 ms |
| Mean draws | 1405.437 |
| Texture evictions | 488 |
| Frames compiling shaders | 0 |

CPU scopes overlap and must not be added. This separate run is slower than the
earlier row-copy comparisons; scene activity and texture work vary, and this is
not a controlled attribution test of the compilation setting. Earlier A/B
windows also had texture evictions. The repeated 0.33 ms blit result survives,
but removing comparison code has not demonstrated an FPS gain.

The remaining evidence points to CPU draw preparation and work outside those
calls. It does not support a GPU bottleneck or a claim of stable frame pacing.
All raw logs, game captures, and generated code remain local.

## Successful-present measurements

Normal builds already keep a fixed ring of QPC timestamps for successful
swapchain presents. Set `RSX_PRESENT_CSV=presents.csv` before launch and close
normally to write it. This avoids enabling per-draw profiling. The new
`scripts/summarize_presents.py` reads the recorded clock frequency and uses
consecutive present IDs, including the interval preceding the first selected
frame. It rejects missing IDs and nonpositive intervals.

```powershell
.venv/Scripts/python.exe scripts/summarize_presents.py <presents.csv> --start-frame 8500 --end-frame 8799 --output <report.json>
```

Cross-checking the unlogged profiling run above gives exactly the same mean
51.143711 ms and 19.552746 FPS from both CSV sources across 300 intervals.
The sources timestamp adjacent points in the present path, so individual
intervals can differ slightly. Present cadence alone does not establish input
latency or correct simulation speed.

## Ordinary playable build baseline

After `scripts/build.ps1 -SpuFma -Test` passed, run `20261012-002732-023`
used `HAZE_RSX_PROFILE=0` and `HAZE_SPU_BYTE_COMPARE=0`. Audio was muted.
Carrier gameplay was verified in capture 21039; the measurement uses the later
600 consecutive successful presents, frames 21500–22099. No per-draw profiler,
job logging, sampling, builds, or captures ran during the window.

| Measurement | Value |
| --- | ---: |
| FPS | 20.681 |
| Mean frame time | 48.355 ms |
| Median frame time | 46.403 ms |
| p95 frame time | 63.900 ms |
| p99 frame time | 74.314 ms |
| Maximum frame time | 91.099 ms |
| Frames above 33.333 ms | 590 / 600 |
| Frames above 50 ms | 226 / 600 |

The game closed normally with exit code 0 and preserved the timing ring. The
ordinary build remains installed for play. This is a fresh baseline, not a
paired test of profiling overhead: NPC activity and texture work differ across
processes. Stable 30 FPS and consistent pacing are still unmet.
