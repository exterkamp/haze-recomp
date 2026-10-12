# Haze fragment depth export — 2026-10-11

## Observed missing contract

Existing local Haze shader captures include control `0300844E`, with the depth
export mask `0x0E` set. Programs at captured offsets `026566C0` and `02656180`
sample neighboring texels, combine depth components, and compute maxima into
fp32 r1.z. The previous decompiler returned color outputs only, so these values
never reached the host depth buffer. The live pipeline key also retained only
the color export bit `0x40`, allowing depth-export state to collide.

This is an observed unsupported rendering contract. Its relationship to the
camera-dependent shadow rectangles and close-soldier visibility still needs
native A/B validation; neither is declared fixed by this implementation alone.
Captured shader bytes and generated shader text stay in ignored local storage.

## Implementation

Explicit control words with any depth-export bit now emit an SV_DEPTH field
from fp32 r1.z. This remains independent of the fp16/fp32 color output mode and
supports one or multiple color targets. Both alpha-test patch paths preserve
the full output structure and discard before depth/color writes. The AUTO
control sentinel retains its legacy color-only behavior.

The live renderer keys the effective depth-export bits and has a diagnostic
`RSX_DEPTH_EXPORT_FILE` 0/1 switch. This experiment is initially disabled so
native comparisons can isolate it from the separately corrected RSX shadow
comparison feature. Successful-present CSVs record both active modes.
`RSX_DEPTH_TRACE` records bounded depth-export program/control states and local
program bytes for identifying which real passes request the feature.

## GPU validation

The full normal regression suite passes. A new asset-free WARP pixel test
performs 640 actual depth-buffer readbacks, with raster depth fixed at 0.5 and
exported depth varied independently between 0.25 and 0.75. It exercises each
individual depth-export bit and their combined mask, both color register files,
single/multiple color targets, inline/buffered constants, alpha discard, LESS
and GREATER depth tests, and depth write masks. A poisoned fp16 h1.z proves
that depth comes from fp32 r1.z even when color uses fp16 exports.

Each case checks both the stored depth and visible color. The no-export cases
must use raster depth, while export cases must use the shader result. The test
vertex shader supplies the decompiler's complete stage signature, including
FOG, so varying-register linkage is exercised correctly.

Existing 512 shadow comparison GPU cases, native D3D12 sampler decoding,
near/far clipping, negative-W rejection and decal bias tests remain green.

## Native visual checks

Muted normal carrier run `20261012-022712-069` confirms that the game actually
executes these programs: trace entries report raw control `0300844E`, the
624-byte pass at color surface `01120000` / zeta `00780000`, and the 272-byte
pass at color surface `01360000` / zeta `01400000`. The switch changes effective
control from `03008440` to `0300844E` and selects a separate pipeline.

Capture 7042 shows a soldier walking very close to the camera fully opaque,
with the nearby red barrel also opaque. This is a successful case, not a
controlled clipping comparison for that soldier or proof that all soldier
visibility defects are fixed. User reports identify soldiers and barrels as
the most affected objects, so both remain required close-up test subjects.

Captures 7836 (corrected shadow comparison and depth export enabled) and 8302
(same tilted view, depth export disabled) still show rectangular floor/shadow
artifacts. These shader omissions alone therefore do not resolve the reported
camera-dependent shadows. Both experiments remain disabled by default.

This run reached its diagnostic timeout before a normal close and did not
flush its present ring. Its recorded CPU windows cannot establish FPS or
frame pacing; neither the complete depth block recording nor the incomplete
shadow recording is accepted as a performance result. A separately identified
repeat run is required for timings.

## Completed normal-build performance comparison

Repeat run `20261012-024229-793` exited normally with code zero and flushed
8,467 successful presents. Capture 2192 verifies carrier gameplay; capture
3642 verifies the stationary close-barrel, tilted-wall view before measurement.
No captures, builds, profilers or other Haze instances ran during the measured
blocks. Audio was muted, the ceiling was 30 FPS, CPU work stayed at 259, depth
clipping followed guest state, and corrected shadow comparison stayed enabled.
Each alternating block has five warmup seconds and twelve measured seconds.

| Depth export | Guest frames | Intervals | FPS | p95 ms | Max ms | Mean process cores |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| Off | 4577–4931 | 355 | 29.640 | 34.069 | 34.721 | 4.543 |
| On | 5081–5434 | 354 | 29.598 | 34.070 | 41.002 | 4.418 |
| Off | 5584–5937 | 354 | 29.612 | 34.089 | 36.133 | 4.504 |
| On | 6087–6441 | 355 | 29.596 | 34.130 | 36.223 | 4.634 |

All 1,418 complete intervals respect the ceiling and none exceeds 50 ms.
The reporter checks both interval endpoints for the selected mode and requires
consecutive present IDs. A separate check confirms the other feature and CPU
work remain constant in every measured row. Raw records and reports are local
`depth-export-repeat-blocks.json` / `depth-export-repeat-report.json`.
This capped view shows no material pacing regression; it cannot establish
uncapped cost, a busy-deck speedup, or a shadow correctness fix.

## References

- [RSX shader control constants](https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Emu/RSX/gcm_enums.h): depth export mask `0x0E` (current raw header checked directly).
- [RSX output register semantics](https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Emu/RSX/Program/FragmentProgramDecompiler.cpp): depth comes from fp32 r1.z.
- [Microsoft HLSL semantics](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-semantics): pixel depth output and its depth-test contract.

These are read-only state/semantics references; no emulator implementation was
copied into this port.
