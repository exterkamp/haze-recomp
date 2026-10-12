# Opening-scene shadow depth clear investigation

The isolated `investigate/shadow-lighting` worktree starts at Haze revision
`6dde832` and SDK revision `45faa42084b89f31eb617732e789f0455b8decc4`.
It shares generated code and game assets with the main checkout, but uses an
independent SDK worktree and copied saves. The requested scope covers idle
flicker, camera-dependent lighting jumps, and approach-related pop-in.

## Confirmed correctness defect

Eight locally captured carrier shadow draws at frame 3419 use zeta address
`01E30000`, surface format `0B0B0128` (Z16), clear register `0000FFFF`, and
depth function `0203` (LEQUAL). The live renderer interpreted every depth
clear as Z24S8, producing `255 / 16777215`, approximately `0.0000152`.
The requested Z16 clear is `65535 / 65535 = 1.0`. A near-zero clear can reject
otherwise valid shadow casters under LEQUAL; that consequence is an inference
from the captured state, not a measured count of rejected native draws.

Z16 uses the low 16 bits of the clear register; Z24S8 uses bits 8 through 31.
The field layout is corroborated by the primary
[RSX register decoder](https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Emu/RSX/rsx_decode.h).
The fix selects the interpretation using the guest surface depth format,
independently of the host resource format. Z24S8 and stencil decoding retain
their existing behavior.

The renderer fix and GPU tests are committed in SDK revision `9058dc4`.

The corrected interpretation defaults on in this worktree.
`RSX_DEPTH_CLEAR_FILE` selects a text file containing `0` (legacy) or `1`
(corrected), polled every 60 presented frames. Successful-present CSVs include
`depth_clear`; the benchmark tool rejects windows crossing mode changes.
Allow at least 60 frames after a mode transition before comparing captures,
because polling occurs after the draw work for that presentation.

## Validation

The full `build.ps1 -SpuFma -Test` gate passes with process exit code zero.
New D3D12 WARP readbacks exercise the actual live clear path at six native,
scaled, asymmetric, and independently sized shadow resolutions. They verify
the legacy wrong value as a negative control, corrected far depth, partial
scissor clears, unused upper Z16 bits, stencil-only depth preservation, and
unchanged Z24S8 depth values. Existing shadow comparison, fragment depth,
window-coordinate, clipping, decal, texture, and runtime tests also pass.

Build with an independent SDK and shared assets:

```powershell
.\scripts\build.ps1 -SharedRoot E:\Code\haze-recomp -SdkRoot .\external\ps3recomp -SpuFma -Test
```

Muted native run `20261012-064737-688` recorded 11,689 successful presents and
closed normally with exit code zero. All 15 requested captures match the
actual successful-present modes. No D3D12 errors or corruption messages were
reported. Warnings remain: 26,117 optimized color-clear mismatches, 7,617
optimized depth-clear mismatches, and 1,018 shader writes to unbound MRT slots.
These warnings are not evidence of an error-free or fully correct image.

The tested candidate executable SHA256 is
`CAB138759DFDBDE9EC6AC7F95AE1BF6C612400FEA77C85BE7FDA0AFDD052C2CE`.
It is preserved at `local/candidates/depth-clear/HazeRecomp.exe`. Captures,
logs, present rows, and `local/native-depth-clear-validation.json` are local
and ignored; no game-derived files are committed.

| Carrier capture | Frame | Clear mode | Shadow comparison |
| --- | ---: | --- | --- |
| Initial deck view | 7186 | Legacy | Off |
| Corrected deck view | 7501 | Corrected | Off |
| Corrected deck, comparison sampler | 7903 | Corrected | On |
| Legacy repeat, comparison sampler | 8653 | Legacy | On |
| Corrected repeat, comparison sampler | 8820 | Corrected | On |
| Forward approach | 9640 | Corrected | On |
| Turned toward nearby barrels | 10615 | Corrected | On |
| Tilted wall view | 11038 | Corrected | On |
| Tilted legacy repeat | 11464 | Legacy | On |
| Tilted corrected repeat | 11686 | Corrected | On |

All comparisons use window-position mode 1 and fragment depth export off.
The corrected repeats show brighter ramp/deck lighting than the legacy
clear captures. Moving helicopter shadows, NPC motion and idle camera sway
prevent a strict pixel-aligned temporal comparison. The nearby barrels and
aircraft remain opaque through the approach and tilt captures; no new
disappearance is visible in these samples.

The investigation confirms and fixes the wrong clear value. It does not
establish that this defect alone causes all idle flicker, camera-dependent
jumps, or distance-related pop-in. The native samples are supporting evidence
for the correction, rather than a complete temporal or streaming regression.

## Other leads

The baseline already includes the validated window-position and depth-clip
fixes described in the neighboring investigation reports. Native run
`20261012-062729-389` retested shadow comparison and fragment depth export with
the window-position correction active. Its 24 requested captures all match
successful-present mode rows; 7,397 presents and a normal exit were recorded,
with no D3D12 debug errors or corruption messages. Close and tilted barrel
views remained opaque, but those comparisons did not establish a fix for all
reported flicker or pop-in.

Run `20261012-063315-169` exercised a 2560x1440 scene with follow-resolution
shadows (5120x5120 backing), including the opening squad cutscene. This starts
at a different chapter phase and is not a direct A/B against the native-size
carrier checkpoint. It reached the runner's ten-minute timeout, so its
successful-present CSV was not flushed and its exit is not a normal shutdown.
The debug log contains no errors or corruption messages. Capture 11394 was
initially labeled with both experiment files enabled, but the log shows that
the actual modes did not change until frame 11400. That capture therefore
does not validate either enabled feature. Mode-file contents alone are
insufficient to validate a capture near a transition.

Haze also requests four constant depth-bias units on a Z16 shadow target while
the renderer uses floating-point depth backing. Direct3D applies
[different depth-bias scales](https://learn.microsoft.com/en-us/windows/win32/direct3d11/d3d10-graphics-programming-guide-output-merger-stage-depth-bias)
to fixed-point and floating-point formats. A typed D16 prototype was saved
locally and removed from the candidate to isolate clear decoding. Its visual
effect and resource/pipeline compatibility remain unvalidated.
