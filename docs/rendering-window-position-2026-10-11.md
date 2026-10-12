# Haze fragment-window coordinates investigation

## Captured evidence

The shadow pass was traced independently of the confirmed clipping fix. Muted
native runs `20261012-025132-485`, `20261012-025852-633`, and
`20261012-030518-776` preserve local pass captures and shader diagnostics.
The rectangular lighting discontinuity is already present in the main world
target at local offset `014D0000`, before the final postprocessing passes.
This rules out a defect introduced solely by the final composite.

RGB previews are insufficient for the half-resolution mask at `01360000`:
its alpha channel contains substantial data while green and blue are zero.
The diagnostic writer can now preserve native RGBA8/RGBA16F rows with
`LD_DUMP_RAW=1`. This also saves host-order register images and current fragment
constants alongside selected draw captures. These files contain game-derived
data and remain local and ignored.

The draw CSV records the actual selected pipeline key, fragment program
location/offset/size/control. `LD_HLSL_DUMP_LIVE_KEYS=1` names shader dumps by
that actual key. The older compatibility key includes changing inline
constants and can overwrite different pipeline variants. In captured frame
3419, all 656 executed draws map to the correct shader/state files using the
new naming. All observed mask texture bindings use remap `0000AAE4`, so an
ignored nonidentity texture remap does not explain this reproduction.

World draws in the register snapshots use `SET_SHADER_WINDOW` (`1D88`) value
`00001240`: window height 576, bottom origin, half-pixel centers. Decompiled
world shaders read WPOS XY to address the half-resolution mask and perform
their own Y inversion. The host originally supplied Direct3D's top-origin
`SV_POSITION` directly. Another captured program reads WPOS W while computing
screen-space texture offsets. These are concrete coordinate-convention leads, rather
than evidence that either change alone resolves the reported shadows.

## Opt-in experiment and tests

`RSX_WINDOW_POSITION_FILE` accepts three settings, polled at presentation
boundaries: 0 preserves the previous behavior, 1 applies the guest window's
origin/center to XY, and 2 additionally converts host W to reciprocal W. The
default is now one after the repeated native comparison below. Mode two
remains an experiment and is not required for this shadow fix. Window
state and reciprocal-W selection are part of pipeline identity; successful
present records include `window_position` for same-process comparisons.

An asset-free D3D11 WARP test performs 3,072 pixel readbacks over top/bottom
origins, half/integer centers, explicit window heights 0/8/576, an offset
viewport, non-unit clip W, buffered/inline constants, alpha testing and MRT
shaders. Z stays unchanged. Mode 1 preserves host W; mode 2 supplies reciprocal
W. Unused WPOS sources remain unchanged, invalid state is rejected, and output
bounds are checked before mutation. The full normal native build/regression
suite passes (`local/window-reciprocal-tests.log`). Benchmark reporter tests
also cover selecting the new mode column and rejecting mixed intervals.

The first test deliberately used clip W=2 and observed host W=2, contradicting
an assumption based on Microsoft's introductory GLSL-porting article. The
Khronos compiler project's documented Direct3D/GLSL distinction agrees with
the measured result. The expected result was separated into host preservation
and explicit reciprocal conversion; no production W change was hidden inside
the origin-only experiment.

## Native checks and limits

Muted normal run `20261012-032258-671` exited normally with code zero and the
30 FPS ceiling selected. Clipping followed guest state; CPU work stayed at
259; depth-export and shadow-comparison experiments stayed disabled.
Captures 5150 and 6628 show opaque nearby soldiers, including an extreme
close-up of two soldiers with complete torsos, arms and helmets. This expands
coverage of the clipping fix beyond the previously verified barrel.

Origin-only switching was observed in the runtime log at frames 5400, 6900,
7200, 7500 and 7920. Stationary downward captures 7374/7600 and tilted captures
7756/7941 do not establish that the reported shadow discontinuity is resolved.
Lighting changes are visible; the opening scripted sequence and changing
exposure prevent treating the earlier captures as a controlled comparison.
No release or correctness claim is based on this run.

A separate user-owned Haze instance ran concurrently throughout this run.
Heavy slowdowns persisted after disabling the experiment. All timing from
this run is excluded from performance conclusions. It does not demonstrate
the candidate's isolated performance or establish the cause of the slowdown.

## Repeated native shadow reproduction

Muted normal run `20261012-033516-014` reached the carrier with mode two active
from frame zero. Forty-four compiled shader variants contain the coordinate
conversion. The two variants reading WPOS W are 272-byte programs at
`0264DB80`, control `02008440`; inspection shows that W scales texture offsets,
so it is not direct evidence of the shadow-mask problem. Shadow comparison
and fragment depth export remained disabled; depth clipping followed the
guest and CPU work remained 259.

Walking toward the left wall and the red/yellow barrels produces a clear
reproduction. At the same camera position, switching mode 2 to 0 restores an
unrelated rectangular brightness boundary across the ground and a horizontal
band across the wall (captures 6338 versus 7075). Tilting down moves the
incorrect wall band (legacy capture 7471). Mode 1 alone removes both artifacts
(7952). Mode 2 looks consistent with mode 1 (8333), then switching back to 0
restores the artifacts (9014), and returning to 1 removes them again (9627).
Runtime logs confirm settings applied before each selected capture.

Two adjacent patches within the same painted ground strip quantify the false
vertical boundary. Their bounds are x=720..734 and x=765..779, y=530..579,
in the 1280x720 RGB capture. Values are mean encoded RGB channel intensity,
not linear-light measurements; the ratio checks spatial discontinuity rather
than interpreting global exposure as a correction. Camera head movement and
moving NPCs are reasons not to demand byte-identical screenshots.

| Capture | Mode | Left mean | Right mean | Right / left |
| --- | ---: | ---: | ---: | ---: |
| 7471 | 0 | 45.528 | 55.384 | 1.2165 |
| 7952 | 1 | 45.131 | 45.659 | 1.0117 |
| 8333 | 2 | 45.065 | 45.627 | 1.0125 |
| 9014 | 0 | 45.772 | 54.944 | 1.2004 |
| 9627 | 1 | 44.982 | 45.599 | 1.0137 |

The nearby barrels and moving soldiers remain opaque. This verifies the
screen-origin correction for the reproduced camera-dependent lighting bug;
it does not establish correctness of every shadow in every level. The other
user-owned Haze process was still running, so this run also contributes no
performance claims. Shader diagnostics and all raw captures remain local.
The process closed normally with exit code zero and flushed 11,133 successful
presents. Across the entire run, the minimum complete interval was 33.4926 ms
and no interval exceeded the 30 FPS ceiling. This is a ceiling check only,
not a representative gameplay rate or pacing benchmark.

The final normal build with mode one as its default passes the complete gate
(`local/window-origin-final-tests.log`), including all 3,072 WPOS readbacks,
512 shadow and 640 fragment-depth readbacks, clipping/decal tests and the real
30 FPS limiter checks. A new isolated performance comparison is still required
before attributing any frame-rate or pacing change to this fix.

## Separate-build benchmark with the controls agent running

At the user's request, performance was measured using the independent rendering
executable in `local/candidates/window-origin-final`, without changing or
stopping the PC-controls worktree's process. Muted normal run
`20261012-034827-435` exited normally with code zero and flushed 9,974 presents.
Captures 3799, 5343 and 9904 verify carrier gameplay and the stationary wall/
barrel view before and after measurement. No captures, builds, shader dumps or
renderer profiler ran during the six measured blocks. All blocks used ten
warmup seconds followed by twelve measured seconds. CPU work stayed 259;
fragment depth export and shadow comparison stayed zero. The reporter verifies
both endpoints' mode and consecutive present IDs for all 1,427 intervals.

The benchmark tool's optional `--observe-pid` reads the other Haze process's
CPU counters without changing its affinity, input, priority or lifetime.
The controls agent's PID 14728 stayed active throughout recording. Process CPU
is expressed as fully busy CPU cores' worth of time, not a system utilization
percentage. Shared GPU activity is not measured by these counters.

| XY correction | Frames | Intervals | FPS | p95 ms | Max ms | Renderer cores | Controls cores |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Off | 6243-6467 | 225 | 18.817 | 69.140 | 93.775 | 4.824 | 4.889 |
| On | 6652-6887 | 236 | 19.761 | 65.879 | 83.885 | 4.758 | 4.953 |
| Off | 7089-7325 | 237 | 19.886 | 64.525 | 103.192 | 4.712 | 5.223 |
| On | 7526-7767 | 242 | 20.325 | 64.043 | 77.498 | 4.950 | 5.144 |
| Off | 7989-8238 | 250 | 20.943 | 64.137 | 103.528 | 4.873 | 4.493 |
| On | 8452-8688 | 237 | 19.902 | 64.629 | 117.472 | 4.951 | 4.980 |

The paired FPS changes have mixed signs; no repeatable slowdown or speedup is
established for the correction under this workload. All intervals respect the
30 FPS ceiling, but pacing remains variable and many intervals exceed 50 ms.
This separate-executable benchmark does not establish isolated gameplay
performance: the other game consumes substantial CPU time and shares the GPU.
It cannot be compared directly to earlier uncontended 29.6 FPS wall samples.
Raw blocks/report remain local as `window-origin-shared-blocks.json` and
`window-origin-shared-report.json`.

The preserved WIP is `local/releases/haze-wip-2026-10-11-shadow-origin`.
Its executable matches the tested candidate SHA-256
`2DB72DCCDBC44DBB19317B1EB0C662963F212159727552E6E7A9048C55511389`.
It includes the clipping and deck-paint fixes, defaults to XY correction only,
and leaves the other rendering experiments disabled. The PC-controls agent's
independent changes are not merged into this rendering WIP.

## References

- [RSX window constants](https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Emu/RSX/gcm_enums.h) and [register decoder](https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Emu/RSX/rsx_decode.h): height/origin/center fields.
- [RSX fragment WPOS convention](https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Emu/RSX/Program/GLSLSnippets/RSXProg/RSXFragmentPrologue.glsl): origin adjustment retains GLSL reciprocal W.
- [Khronos compiler investigation of Direct3D versus GLSL W](https://github.com/KhronosGroup/glslang/issues/2244): the two fragment-coordinate W values are reciprocals.
- [Microsoft GLSL porting article](https://learn.microsoft.com/en-us/windows/uwp/gaming/port-the-glsl): the W claim here disagrees with the actual WARP readback above; it was not used to override measured behavior.

These were read-only references. No emulator code or licensed shader programs
were copied into the port or the synthetic tests.
