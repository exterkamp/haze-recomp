# Specialized vertex loops — 2026-10-11

The carrier's renderer CPU profile concentrated samples in batch F32 byte
swaps, SINT16 decoding, and scalar color conversion. In the focused 20-second
sample, `rsx_vertex_fetch_many` had 558 instruction samples and the general
decoder had 292. Assembly confirmed the F32 loop stored/swapped each component
separately and UNORM8 used separate scalar conversions/divisions. Sample
counts include waits elsewhere and are not utilization percentages.

## Change and correctness

Batch fetch now selects F32, UNORM8, and UINT8 size-specific loops once per
attribute. F32 components are byte-swapped together; byte colors convert their
lanes together. Normalized values retain division, rather than multiplication
by a rounded reciprocal, to preserve exact reference results. Missing lanes
keep `(0,0,0,1)` defaults. Three-component loads read exactly 12/3 bytes.

The complete reference range is checked before writing. Reused plans whose
references escape that range, inline data, divisors, and unresolved mappings
retain the previous batch/resolver path. Other formats retain their existing
decoders, including the earlier SINT16 SIMD path. No vertices or draws are
discarded. The generic scalar decoder remains an independent oracle.

The full suite passed. Added checks compare 49,152 raw F32/color cases with
the scalar decoder, including NaN payloads, signed zeros, infinities,
subnormals, every byte value, all component counts and unaligned pointers.
Protected source/destination tails and reused-plan fallbacks also pass.
Existing multi-vertex tests cover formats, disabled attributes, divisors,
20-bit wrapping, inline draws, caches, remapping, and WARP rendering.

## Same-process comparison

Muted run `20261012-003556-116`, visually verified carrier in capture 9878;
optimized path checked again in capture 12878. Each block contains 180
consecutive frames after 80 warmup frames. Heap SPU contexts, normal SIMD SPU
helpers, coherent row copies, bulk references and dense remapping remain on.
Live SPU comparison code is absent. `RSX_PROFILE_LIGHT=1`; warm shader cache.
No builds, readbacks, instruction sampling, or job logging during the blocks.

| Path | Frames | FPS | p95 ms | Vertex decode ms | Vertex fetch/pack ms | Draw CPU ms | Mean draws |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Previous | 10605–10784 | 22.517 | 58.444 | 4.529 | 8.252 | 24.662 | 1461 |
| Specialized | 10941–11120 | 23.165 | 56.373 | 3.380 | 7.080 | 22.484 | 1444 |
| Previous | 11241–11420 | 25.003 | 54.579 | 4.436 | 7.985 | 21.178 | 1236 |
| Specialized | 11541–11720 | 25.223 | 51.963 | 3.200 | 6.673 | 21.227 | 1332 |
| Previous | 11841–12020 | 23.912 | 55.645 | 4.334 | 7.848 | 23.768 | 1380 |
| Specialized | 12141–12320 | 25.041 | 51.970 | 3.054 | 6.401 | 21.755 | 1381 |

The measured decoding saving repeats at about 1.15–1.28 ms/frame. FPS and p95
favor the specialized path in all three pairs, but NPC and draw-count changes
prevent assigning their entire difference to the optimization. Process CPU
use falls in two pairs and rises slightly in the middle pair; its counter
window includes propagation and warmup. Specialized loops become the default.
The process closed normally with exit code 0. This does not establish sustained
30 FPS. The user's revised target permits dips and caps the maximum at 30 FPS.

## Reproduction

Build `scripts/build.ps1 -SpuFma -ProfileRenderer -Test`. Set
`RSX_PROFILE_LIGHT=1`, `RSX_PROFILE_CSV=renderer.csv` and
`RSX_CPU_WORK_FILE` to an ignored control file initially containing 3.
Leave SPU and blit control files unset so their production defaults apply.
Verify carrier gameplay, then run:
Use `scripts/run.ps1 -Uncapped` for comparisons after the 30 FPS limiter lands.

```powershell
.venv/Scripts/python.exe scripts/benchmark_cpu_work.py <renderer.csv> <control.txt> --flags 3 259 3 259 3 259 --pid <HazePID> --output <blocks.json>
```

Bit 256 selects specialized loops. Remove the diagnostic control after the
comparison; the driver resets it to zero on exit. CPU scopes overlap. Assets,
generated translations, raw profiles, and captures remain local.
