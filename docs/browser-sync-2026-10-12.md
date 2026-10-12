# Browser integration and measurements, 2026-10-12

The browser worktree incorporates `haze-recomp` main through `4d8f420` and
`ps3recomp` haze through `59fbc8d`. SDK merge `5862cb9` and port merge `198b91f`
retain the Wasm runtime, WebGL backend, local assets, audio, and browser input.
The native scheduling and frame-pacing experiments retain their platform guards
and defaults. D3D12 optimizations do not automatically optimize WebGL.

The portable engine also receives the confirmed Z16 shadow-clear correction
in SDK commit `455a022`. Regression cases cover Z16 far/zero depth and Z24S8
depth/stencil packing. Browser mouse motion feeds the native distance-based
camera hook instead of emulating short right-stick impulses.

The default page fills the viewport and hides controls and logs. Its first click
requests browser fullscreen and enables audio, including clicks before audio
initialization finishes. `?debug=1` restores diagnostics. Normal log messages
no longer rebuild or scroll a DOM log; diagnostic log updates are batched.

Presentation copies the WebGL canvas into a compositor canvas and transfers
that copy. Direct ImageBitmap transfer detached the WebGL drawing buffer and
caused recurring CPU pixel readback. Thirty-two successive GPU test presents
retain the drawing buffer and never enter the pixel fallback.

## Validation

- Portable draw-engine regression: all checks passed, including depth clears.
- Final WebGL smoke: passed with correct canvas pixels, distinct queued indexed
  draws/constants, and repeated GPU handoffs;
  evidence `local/wasm-tests/20261012-100439/result.json`.
- Immersive UI/audio check: real fullscreen, viewport-sized canvas, hidden
  controls, empty DOM log, and audio unlocked by an early click. AudioWorklet
  consumed 96,000 samples with no underruns or drops;
  evidence `local/wasm-immersive-check.json`.
- Native input/camera regressions: passed after merge conflict resolution;
  evidence `local/wasm-sync-input-tests.log`.
- Campaign: 451 seconds without browser errors. Actual fullscreen, game audio,
  a single 450-count mouse movement, WASD movement, and pause/resume were
  exercised; evidence `local/wasm-tests/20261012-094255/`. Screenshots 0314 and
  0325 show the changed view and pause menu. The automated report's pass flag
  applies only to smoke runs; it does not certify full campaign progression.

## Performance remains unresolved

Completed frames received by the page were counted against `performance.now()`
over approximately 20-second windows. The camera stayed fixed during each
comparison; the running game and NPC animation were not deterministic.

| Carrier-deck view / mode | Frames | Seconds | FPS |
| --- | ---: | ---: | ---: |
| Initial view, timer | 111 | 20.030 | 5.54 |
| Initial view, immediate scheduler | 120 | 20.050 | 5.99 |
| Initial view, immediate scheduler again | 125 | 20.034 | 6.24 |
| Initial view, timer again | 135 | 20.038 | 6.74 |
| Turned view, framebuffer checks | 172 | 20.040 | 8.58 |
| Turned view, checks temporarily bypassed | 173 | 20.020 | 8.64 |

The scheduler's two-window means are effectively the same. Framebuffer queries
accumulated 6.243 seconds over 420 calls in a separate diagnostic interval, but
omitting those queries did not improve the measured frame rate; the wait moved
elsewhere. Neither experiment is enabled in normal play. No material FPS gain
from the branch sync or presentation change is claimed. The renderer and guest
work still need further profiling; this build is not yet smooth gameplay.

A further GPU upload experiment streamed vertex, index, and constant data into
large buffers with per-frame orphaning. Indexed rendering and distinct queued
constants passed pixel checks, but the actual campaign delivered only 16 frames
in 19.842 seconds (0.81 FPS), and menus also slowed. The experiment was removed;
the hosted build retains the earlier upload path. Evidence:
`local/wasm-tests/20261012-095940/` and `local/wasm-stream-game-test.log`.

Raw measurements: `local/wasm-sync-measurements.json`. Browser: Chromium with
ANGLE on the RTX 5080 test machine.
