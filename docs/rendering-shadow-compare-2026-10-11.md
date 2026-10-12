# Haze shadow comparison investigation — 2026-10-11

## Observed state and missing behavior

In normal muted native run `20261012-013909-168`, the first carrier depth
binding at frame 2193 is unit zero, local offset `01E30000`, depth16 linear
format `B2`, with a 2048×2048 snapshot. Address state is `60030303`, filter
`02022000`, remap `0000AAE4`, and control0 `80000000`. Haze requests LEQUAL
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

The renderer decodes the six active NV40 RCOMP modes and includes shadow-unit
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
Tests of the actual D3D12 sampler decoder verify Haze's LEQUAL/linear state,
NV40 comparison mapping and cache identity. Existing decal/depth clipping GPU
readbacks and the full native suite remain green.

The experiment is currently disabled by default. `RSX_SHADOW_COMPARE_FILE`
accepts 0/1 at presentation boundaries for same-process comparisons. Successful
present CSVs record the active mode. `RSX_DEPTH_TRACE` captures bounded distinct
depth/program states and writes local shader bytes into the ignored run
directory; these game-derived bytes are never committed.

Visual carrier A/B checks and performance validation are pending. An unrelated
user-owned game session was running during the first experiment, so its timings
are not a performance baseline. Automated runs remain muted and use isolated
test saves.

## Hardware and API references

These are read-only state/semantics references, not imported emulator code:

- [NV30/40 texture register definitions](https://github.com/envytools/envytools/blob/master/rnndb/graph/nv30-40_3d.xml): RCOMP numbering and address fields.
- [RSX texture register accessors](https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Emu/RSX/RSXTexture.cpp): comparator, LOD and alpha-kill fields.
- [RSX texture state selection](https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Emu/RSX/RSXThread.cpp): active depth comparator selection.
- [RSX fragment instruction semantics](https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Emu/RSX/Program/FragmentProgramDecompiler.cpp): projected comparison and result lane semantics.
- [Microsoft SampleCmp](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-to-samplecmp): comparison occurs per sample before filtering.
