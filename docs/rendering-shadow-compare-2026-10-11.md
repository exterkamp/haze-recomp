# Haze shadow comparison investigation — 2026-10-11

## Observed state and missing behavior

In normal muted native run `20261012-013909-168`, the first carrier depth
binding at frame 2193 is unit zero, local offset `01E30000`, depth16 linear
format `B2`, with a 2048×2048 snapshot. Address state is `60030303`, filter
`02022000`, remap `0000AAE4`, and control0 `80000000`. Haze requests GEQUAL
comparison with linear filtering and normalized coordinates. The renderer used
ordinary texture sampling and never configured a comparison sampler.

The subsequent muted experiment `20261012-015829-554` captured the actual
fragment programs using that depth binding. The 640-byte and 768-byte programs
each contain three TXP instructions on unit zero. They require XY/W texture
coordinates and Z/W comparison reference; a plain depth read loses the latter.
Programs without texture instructions also retain that binding, so binding
counts alone do not describe how many shaders actually sample it.

This is an observed missing feature, and a candidate explanation for the
camera-dependent shadows. It is separate from the confirmed barrel depth clip
fix. It does not yet establish that every invisible soldier has the same cause.

## Implementation and validation

The experiment adds typed depth textures and comparison samplers to the pixel
decompiler for TEX and TXP. Comparison results fill all four lanes. Ordinary
and cube textures can coexist with a shadow unit. Texel-coordinate scaling
affects UVs only, preserving the comparison reference. The old APIs retain
byte-identical ordinary shader output.

The renderer decodes the six active RSX comparison modes and includes shadow-unit
selection in pipeline identity. Sampler identity is widened to preserve both
12-bit LOD fields, R addressing, comparison enable and comparison function;
the previous 32-bit packing lost state. NEVER/ALWAYS, alpha-kill, color textures,
cube textures and unsupported dimensions do not select the shadow path.
Shadow TXB/TXL are explicitly rejected rather than silently compiled as a raw
read; the observed Haze programs use TXP.

The normal native regression suite passes, including 512 D3D11 WARP pixel
readbacks with a synthetic 2×2 depth pattern. These cover TEX/TXP, eight host
comparison functions, point filtering and compare-before-filter PCF, equality,
projected references, normalized/texel UVs, and buffered/inline decompilation.
Mixed ordinary/cube shader compilation and bounded-output checks also pass.
Tests of the actual D3D12 sampler decoder verify Haze's GEQUAL/linear state,
RSX comparison mapping and cache identity. Existing decal/depth clipping GPU
readbacks and the full native suite remain green.

The experiment is currently disabled by default. `RSX_SHADOW_COMPARE_FILE`
accepts 0/1 at presentation boundaries for same-process comparisons. Successful
present CSVs record the active mode. `RSX_DEPTH_TRACE` captures bounded distinct
depth/program states and writes local shader bytes into the ignored run
directory; these game-derived bytes are never committed.

## First prototype native results and correction

Muted normal carrier run `20261012-020457-554` exited with code zero. Depth
clipping followed guest state throughout. Captures 7044/8211 compare on/off at
the nearby barrel, and 8870/9356 compare on/off after tilting the camera down.
The barrel remains opaque, but rectangular shadow artifacts persist in both
modes. No draw drops or shader compilation failures were observed.

Six normal-build timed blocks at that stationary tilted view retained every
present interval. There were no captures, builds, profiler sessions or other
Haze processes during these blocks. CPU work stayed at 259. Each block had five
warmup seconds and twelve measured seconds; mode settlement and consecutive
present IDs were validated from the flushed CSV.

| Prototype mode | Guest frames | FPS | p95 ms | Max ms | Mean process cores |
| --- | --- | ---: | ---: | ---: | ---: |
| Off | 10567–10921 | 29.649 | 34.043 | 35.918 | 5.635 |
| On | 11071–11424 | 29.651 | 34.031 | 37.670 | 5.574 |
| Off | 11575–11929 | 29.646 | 34.070 | 34.562 | 5.568 |
| On | 12078–12432 | 29.650 | 34.048 | 36.670 | 5.713 |
| Off | 12582–12936 | 29.681 | 34.011 | 35.547 | 5.806 |
| On | 13087–13439 | 29.549 | 34.119 | 48.096 | 5.493 |

All 2,127 complete intervals respect the 30 FPS ceiling, with zero over 50 ms.
This is a capped close-wall observation, not a busy-deck speedup or a validation
of the final sampler path. The prototype used the older PC NV40 RCOMP table,
which reverses LESS/GREATER relative to RSX. The current upstream RSX enum maps
raw comparison six to GEQUAL, not LEQUAL. The decoder and independent expected
mapping have been corrected; these timings belong to the initial prototype and
must not be presented as final comparison-sampler performance.

The corrected mapping was subsequently exercised in muted normal carrier run
`20261012-022712-069`, separately from the fragment depth-export feature.
Captures 5051/6586 compare shadow sampling off/on at the nearby barrel wall;
7042 adds depth export and includes a fully opaque soldier very close to the
camera. Captures 7836/8302 compare depth export on/off after camera tilt, with
the corrected shadow sampler enabled. Rectangular shadow artifacts persist.
This is evidence against these omissions alone explaining the visible problem,
not evidence that the missing hardware features should be discarded.

That run timed out without flushing the present ring. Its CPU block records
are excluded from FPS/frame-pacing conclusions; the shadow recording is also
incomplete. Both experiments remain off by default pending further rendering
validation. Automated tests remain muted and use isolated saves. One opaque
close-up NPC does not establish that every reported soldier case is repaired.

## Corrected sampler performance comparison

Muted normal carrier repeat `20261012-024229-793` exited normally with code
zero and preserved 8,467 presents. After the separate depth-export blocks, four
alternating corrected-sampler blocks ran at the same stationary tilted-wall
view. Depth export remained enabled and CPU work remained 259 in every measured
row; clipping followed guest state. Each block has five warmup seconds and
twelve measured seconds, with no captures, builds, profilers or other Haze
processes during measurement.

| Corrected shadow comparison | Guest frames | Intervals | FPS | p95 ms | Max ms | Mean process cores |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| Off | 6597–6950 | 354 | 29.623 | 34.071 | 36.081 | 4.659 |
| On | 7100–7454 | 355 | 29.623 | 34.059 | 37.980 | 4.763 |
| Off | 7604–7957 | 354 | 29.604 | 34.061 | 36.396 | 4.707 |
| On | 8107–8461 | 355 | 29.619 | 34.077 | 34.265 | 4.726 |

All 1,418 complete, consecutive intervals respect the 30 FPS ceiling and none
exceeds 50 ms. Mode settlement is checked at both interval endpoints. Local
records are `shadow-corrected-repeat-blocks.json` and
`shadow-corrected-repeat-report.json`. These timings supersede the incorrect
prototype as evidence for the current sampler path. They show no material
pacing regression at this capped view, not its uncapped cost or a busy-deck
speedup. Visual shadow correctness remains unresolved.

## Hardware and API references

These are read-only state/semantics references, not imported emulator code:

- [RSX comparison enums](https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Emu/RSX/gcm_enums.h): raw comparison numbering; the older PC NV30/40 table is not interchangeable.
- [RSX texture register accessors](https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Emu/RSX/RSXTexture.cpp): comparator, LOD and alpha-kill fields.
- [RSX texture state selection](https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Emu/RSX/RSXThread.cpp): active depth comparator selection.
- [RSX fragment instruction semantics](https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Emu/RSX/Program/FragmentProgramDecompiler.cpp): projected comparison and result lane semantics.
- [Microsoft SampleCmp](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-to-samplecmp): comparison occurs per sample before filtering.
