# Close-range model rendering and depth clipping

The user confirmed the deck paint flicker was gone in the first preserved WIP,
but reported soldiers/barrels appearing invisible or in the wrong layer up
close, and shadows changing when the camera tilts. These reports remain broader
than the particular case reproduced below; all rendering bugs are not resolved.

The live renderer zero-initialized its rasterizer state and never assigned
`DepthClipEnable`. It therefore disabled near/far depth clipping on every draw.
The dispatcher also left ZMIN_MAX_CONTROL at zero instead of its reset value
of one. A raw read could not distinguish an unwritten register from an explicit
guest request to disable clipping.

Microsoft documents the clipping sequence and the effect of disabling its Z
step in [D3D12_RASTERIZER_DESC](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_rasterizer_desc).
The register reset and field numbering were checked against the upstream
[RSX reset image](https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Emu/RSX/rsx_methods.cpp)
and [register decoder](https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Emu/RSX/rsx_decode.h)
as read-only hardware-state references; no emulator implementation was copied.

## Controlled native reproduction

Muted run `local/runs/20261012-011728-601` exited normally with code zero.
The camera was moved from the carrier opening to the barrels beside the vehicle
and then stopped. The diagnostic file changed only host depth clipping at
presentation boundaries; the game, shaders, depth resources and camera were
shared between conditions. Captures are visual evidence, not FPS samples.

| Capture | Host Z clipping | Observation |
| --- | --- | --- |
| requested_011501 | On | Nearby yellow barrel is opaque |
| requested_012466 | Off | Incorrect translucent-looking barrel; background/shadow overlay crosses it |
| requested_013375 | On | Opaque barrel returns |
| requested_014773 | On, camera tilted slightly down | Yellow barrel stays opaque |
| requested_015986 | Off, same tilted view | Incorrect barrel overlay returns |
| requested_016586 | Off, camera turned toward squad | Floor strip crosses nearby red barrel |
| requested_017227 | On, same heading | Red barrel is opaque |

Idle camera sway and NPC motion continue, so these are not identical pixel
images. Other shadow artifacts remain visible, including a large vertical
shadow boundary. The comparison supports this close-barrel lead; it does not
prove that every missing soldier or camera-dependent shadow has the same cause.

## Change and validation

The dispatcher now seeds ZMIN_MAX_CONTROL to one. The renderer takes clipping
from its low nibble, puts it in PSO identity, and assigns DepthClipEnable.
Explicit guest changes therefore select the corresponding rasterizer state.
The diagnostic `RSX_DEPTH_CLIP_FILE` accepts -1 to follow guest state, 0 for the
previous clipping-disabled path, or 1 to force clipping. It is absent during
ordinary play. `RSX_DEPTH_TRACE` logs bounded distinct depth states, including
whether the guest actually wrote the control register.

The full profiling and normal native suites passed. The existing asset-free
WARP test now reads pixels produced by the actual render-state implementation:
geometry beyond near/far planes is rejected when clipping is requested and
rendered when disabled; valid geometry survives in both modes; negative W is
rejected in both. It also verifies reset state, guest register changes and PSO
key separation. All previous coplanar decal/bias readbacks still pass.

## Default-state native follow-up

Muted normal run `local/runs/20261012-013909-168` exited with code zero and
preserved 15,921 successful presents. Its carrier trace reports raw control one,
`control_written=0`, and host clipping enabled: Haze uses the seeded reset state
without overriding it. Capture `requested_011243` shows an opaque nearby red
barrel with no floor stripe drawn across it, after walking toward the props.
This confirms that ordinary gameplay takes the corrected path without a probe.

The stationary carrier window 4000–4599 contains 600 consecutive presents at
20.68 FPS, with 48.35 ms mean, 65.84 ms p95 and 33.51 ms minimum. This is a
normal-build observation, not a speedup comparison; no interval exceeds the
30 FPS ceiling. The surrounding loading/menu intervals are excluded.

The shadow investigation remains open. Actual depth binding at frame 2193 uses
linear depth16 (`B2`), 2048×2048, wrap `60030303`, filter `02022000`, remap
`0000AAE4`, and control0 `80000000`. The wrap high nibble requests GEQUAL depth
comparison, which the renderer's ordinary sampler previously ignored. This is
a separate, observed shadow lead; soldier visibility and every camera-dependent
artifact are not yet verified as fixed.
