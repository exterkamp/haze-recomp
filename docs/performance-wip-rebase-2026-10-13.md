# Rebased combined performance WIP

The performance branch is rebased onto `main` commit `4adab80`, including SDL
gamepads, live input rebinding and PC prompt texture replacement. SDK merge
`9f44d36` retains all performance changes and merges main's SDK dependency
`8660186`; current SDK `haze` is an ancestor too. Root compiled source is
`fe97822`; subsequent `a4167ed` fixes only the camera test's input include path.

The SDK merge preserves native texture mutation tracking and forces refresh
when a prompt atlas revision changes, even within the same frame and without
guest-memory mutation. Both full-hash and MEM_WRITE_WATCH variants now verify
actual GPU pixels, unchanged-source cache reuse, prompt revision refresh and
fallback to the original texture. Both pass in the full regression suite.

The initial full build compiled successfully but stopped at the camera test:
its new gamepad header requires the SDK input include directory. After fixing
that test command, `build.ps1 -SpuFma -Test` passes in full. Profiling is disabled
and SDL 3.4.18 is linked statically. No game/generated assets are committed.

Executable SHA256:
`B41294EDBFC2FF024BFB402BEE8ADB2F5E474E8AF429CD9FB6C98C48D759C7E6`.

Local delivery: `local/releases/haze-wip-2026-10-13-performance/Play-Haze.cmd`.
The folder contains the executable, one fixed performance launcher, graphics
configuration, README and hash. It uses this checkout's game files, normal
saves, input configuration and prompt assets. Interactive audio is enabled.
Rendering defaults are 2560x1440/borderless, native shadows, VSync on, uncapped.
GPU occurrence mode, native index preparation and the bounded VP consumer are
enabled for this experimental WIP. Producer batching and shortened job polling
remain off. This delivery is not a declaration of campaign/144-FPS readiness.

## Exact-binary smoke and frame measurements

Muted run `20261013-040224-970`, prefix `local/rebased-performance-smoke`, reaches
the initial carrier. Logs confirm SDL initialization, keyboard atlas readiness
and fingerprint-matched replacement of the game's padbuttons atlas. Capture
`requested_011764.ppm` verifies the scene before measurements. Automated input
and save state are private. The owned process closes normally, exit 0.

Two 24-second absolute camera sweeps follow five-second warmups, with native
consumer fixed on. Rendering is 1440p, shadows follow rendering, output window
1280x720, VSync on and no cap. These are the comparison settings, distinct from
the delivered launcher's native-shadow/borderless defaults. No profiling,
capture, builds, test executables or instruction sampling run in these windows.
Both windows pass present, camera, mode and process-bracketing checks.

| Frames | FPS | Mean ms | p95 ms | p99 ms | Maximum ms |
|---:|---:|---:|---:|---:|---:|
| 640 | 26.71 | 37.44 | 49.96 | 59.40 | 69.97 |
| 698 | 29.12 | 34.34 | 45.94 | 52.35 | 67.54 |

Labyrinthine remained closed and no second Haze was observed. Host CPU activity
was approximately 82.1% and 73.6%, versus 61-64% in the preceding pre-rebase
comparison, which averaged about 31-32 FPS. Background workload is not isolated
by the Haze observer. These fresh results are slower, but this cross-session
comparison cannot distinguish a rebase regression from changing host load.
They must not be advertised as an FPS improvement. A matched binary comparison
under steady load remains open, as do broad gameplay and pacing validation.

## Tracking reconciliation

Issue bodies #9, #32, #33, #34, #35, #38 and #39 now distinguish checked
implementation/measurement milestones from unchecked full acceptance gates.
The substantive remaining work is retained: persistent GPU residency, complete
native submission classes, job readiness/publication attribution, campaign
validation and stable unlocked pacing. Experimental scope savings are not
marked as completed whole-frame targets.
