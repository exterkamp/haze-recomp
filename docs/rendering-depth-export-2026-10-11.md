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

## References

- [RSX shader control constants](https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Emu/RSX/gcm_enums.h): depth export mask `0x0E` (current raw header checked directly).
- [RSX output register semantics](https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Emu/RSX/Program/FragmentProgramDecompiler.cpp): depth comes from fp32 r1.z.
- [Microsoft HLSL semantics](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-semantics): pixel depth output and its depth-test contract.

These are read-only state/semantics references; no emulator implementation was
copied into this port.
