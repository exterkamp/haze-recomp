# Custom internal rendering resolution

Originally implemented on `feat/custom-render-resolution`. Further graphics
settings now live on `feat/graphics-settings` in
`.worktrees/custom-render-resolution`, with an independent SDK worktree on
the same branch name. See [graphics settings](graphics-settings.md) for the
startup configuration, including independent shadow resolution. The main
checkout and PC-controls worktree are separate.
Local game files, generated translations and development tools are shared
through junctions; build products and diagnostic runs belong to this worktree.

## Selecting a resolution

```powershell
.\scripts\build.ps1 -SpuFma -Test
.\scripts\play.ps1 -RenderResolution 1920x1080
.\scripts\run.ps1 -Seconds 180 -RenderResolution 1777x997 -CaptureFrames 2
```

The executable also accepts `RSX_RENDER_RESOLUTION=WIDTHxHEIGHT`. Launcher
arguments override that environment setting. An absent or empty setting retains
native rendering. Dimensions are strict decimal integers from 1 through 8192;
actual allocations remain subject to GPU memory and the D3D12 texture limit.
The diagnostic launcher rejects custom resolution with its Legacy backend.
Changing resolution requires restarting the process.

The window's logical/client dimensions stay independent of the internal GPU
dimensions. DXGI presents the selected resolution into that window. Changing
aspect ratio stretches the original projection and HUD; this feature does not
implement ultrawide projection or HUD placement.

## Rendering changes

Native captures show Haze declaring 1024x576 main scene targets and a 1280x720
final scanout. The port supplies the former as the renderer's reference size.
In custom mode, both the main scene and final scanout reach the requested size.
Other targets scale relative to the main scene, with integer rounding. Native
mode preserves every original target size, including the 576p scene.

| Guest target | Native host target | Host at 1920x1080 | Host at 1777x997 |
| --- | --- | --- | --- |
| Main scene, 1024x576 | 1024x576 | 1920x1080 | 1777x997 |
| Half scene, 512x288 | 512x288 | 960x540 | 889x499 |
| Quarter scene, 256x144 | 256x144 | 480x270 | 444x249 |
| Final scanout, 1280x720 | 1280x720 | 1920x1080 | 1777x997 |
| Square target, 2048x2048 | 2048x2048 | 3840x3840 | 3554x3545 |

Color surfaces retain separate guest dimensions for cache identity, texture
selection and address/pitch calculations. Their actual resources, feedback
copies, viewport and readbacks use host dimensions. Scissor and clear edges
map into that host space, including channel-masked clears and empty rectangles.
GPU copies similarly translate guest rectangles. Depth backing and sampled
depth snapshots follow the scaled clip dimensions.

Fragment WPOS is converted back to guest pixel space before the existing RSX
window-origin and pixel-center conversions. This keeps Haze's shader constants,
screen-space effects and texel-addressed coordinates in their original units.
The exact ratio for each rounded target enters shader identity. Normalized
sampling then reads the higher-resolution GPU results through the existing
render-target bindings. Asset textures and guest allocations are unchanged.
Host-decoded movie uploads and overlay readbacks also follow the swap-chain
dimensions; Haze's media decoding remains outside this feature.

Scaling intermediate and square targets raises GPU work and memory use. These
changes do not claim a frame-rate improvement or remove existing rendering bugs.

## Verification

The full `scripts/build.ps1 -SpuFma -Test` gate passes, including the existing
runtime/input/SPU regressions and these extended renderer checks:

- 12,288 WARP fragment-coordinate readbacks covering native, 1.5x, 2x and
  nonstandard scaling, both origins and centers, preserved Z, both W modes,
  inline/buffered constants, alpha testing and MRT.
- Real D3D12 surface/depth allocations at native, 1.5x, 2x and fractional
  asymmetric dimensions. Main scene and larger scanout declarations both map
  to the requested size when a smaller scene reference is supplied.
- GPU readback of every destination pixel after the actual live clear and
  blit functions: fractional scissor edges, red-only clear preserving blue and
  alpha, and full-surface transfers. Guest address containment rejects ranges
  beyond the original guest allocation despite larger host resources.
- Strict resolution parser rejection, native-size preservation, launcher
  parsing and rejection of oversized dimensions/Legacy combinations.

Native evidence remains in ignored local files:

| Run | Selected resolution | Evidence |
| --- | --- | --- |
| `20261012-041502-761` | 1920x1080 | Scene/scanout allocations both 1920x1080; carrier capture `requested_003225.ppm`; camera turn `requested_003446.ppm` |
| `20261012-041832-462` | 1777x997 | Scene/scanout allocations both 1777x997; fractional intermediate targets; carrier capture `requested_002370.ppm` |
| `20261012-042203-094` | Native default | Final candidate boots with 1024x576 scene and 1280x720 scanout; title capture `frame_000750.ppm` |

All three runs use the D3D12 debug layer. Their complete logs contain no
D3D12 error/corruption message or resource/presentation
failure. Existing optimized-clear-value mismatch warnings remain. Captures were
visually inspected for the scene, soldiers, carrier, sky and UI. The 1080p input
probe visibly turns the camera. These are opening-scene checks, not campaign
completion, an exhaustive visual comparison, or a performance benchmark.

Full gate logs: `local/reference-tests.log` and
`local/release-resolution-tests.log`. Focused updated GPU checks:
`local/test-resolution-final.log`.

The tested source is preserved in SDK commit `8037e4a`. The final candidate
`local/candidates/custom-resolution-final/HazeRecomp.exe` has SHA-256
`FD459B0B94FE2F6D25DBAA8A7F66F5167A1D270DE0F92AA8B4BCBA8784EBB446`.
The default `build/haze/HazeRecomp.exe` was also rebuilt from this source.
The diagnostic runs end at their configured timeouts; those timeouts establish
neither campaign completion nor a clean application shutdown.
