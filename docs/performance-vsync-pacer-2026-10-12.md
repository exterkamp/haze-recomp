# Native pacing selection and VSync validation

The PC `frame_limit` setting now selects the guest-wait bypass automatically
for 0 (unlimited) and 31..240. Values 1..30 retain the established guest wait.
`HAZE_NATIVE_FRAME_PACER=0/1` still overrides that selection, and the diagnostic
`HAZE_FRAME_PACER_FILE` can still switch the running process. Layout guards and
the original update/clock/submission code are unchanged. Higher-rate gameplay
timing remains experimental; removing the 30 FPS wait does not remove CPU
bottlenecks or establish consistent pacing.

The ordinary candidate is `local/candidates/native-frame-settings/HazeRecomp.exe`,
SHA256 `2651123FB7C7C76DC3C328B751D16BF867BC7DC4D3B24E247BD691FAD61036F0`.
The full FMA build and regression suite passed, including the new selector
boundary/override tests. A focused graphics configuration test also passed
after adding actual INI-to-environment-to-selector assertions: cap 60 selects
the bypass, explicit override 0 disables it, cap 30 retains the wait, and an
absent optional config retains the capped default. Invalid and overflowing
selector input fails closed.

Muted ordinary run `20261012-154326-870` then validated the new selection
without `HAZE_NATIVE_FRAME_PACER` or `HAZE_FRAME_PACER_FILE` set. The INI alone
selected cap 0 and VSync on; the main-thread mode event (host TID 56748) reports
`enabled=1`. Capture 029668 verifies carrier gameplay and 030507 verifies the
response to camera input. A separate read-only sky clock sample records 94
updates and 94 presents in 2.999417 wall seconds (31.339 updates/second), with
3.052897 seconds of sampled game time. These non-atomic endpoints do not prove
exact update/present correspondence or frame timing. Another Haze was running,
so this is a settings/functional check, not an isolated FPS benchmark. Normal
close returned exit code zero and flushed the present CSV. Artifacts:
`local/native-frame-settings-{clock.json,clock-summary.json,presents.csv}`.

## VSync-on functional experiment

Earlier muted run `20261012-152649-380` used the manual control-file selection
in candidate `local/candidates/native-triplet/HazeRecomp.exe`, SHA256
`CB3555545FB184B45E29D8FAD29F0D2E8639CC7440DFE7524A8FB3B8B4737947`.
It rendered at 2560x1440, follow-resolution shadows, AA off, AF1, host cap zero,
VSync on, FMA, native PPU queue waiting and native SPU idle waiting. Native
triplet copy remained disabled. Real audio processing/device timing remained
enabled with muted output. No job/renderer timing instrumentation was enabled.

Boot logs report `vsync=on` and `present_sync=1`. These validate the requested
DXGI present interval, not physical scanout timing. Captures 036172 and 042361
show the sky-facing view before and after the four 30-second blocks. Each block
had five warmup seconds; no input, capture or thread sampling occurred inside
the windows. Mode events from host thread 51980 validate original/bypass/bypass/
original selection. The game closed normally with exit code zero, retaining
complete present CSV coverage.

Another Haze from `.worktrees/shadow-lighting` started after launch and was
observed throughout all timed windows. The strict isolated report rejects the
session. The following retained diagnostic figures are explicitly contended:

| Guest wait | FPS (contended) | p95 frame, ms | p99 frame, ms | Mean CPU cores |
| --- | ---: | ---: | ---: | ---: |
| Original 1 | 28.319 | 43.676 | 82.081 | 3.215 |
| Bypass 1 | 36.414 | 46.216 | 63.986 | 3.605 |
| Bypass 2 | 30.791 | 55.252 | 73.387 | 3.283 |
| Original 2 | 27.341 | 49.005 | 66.593 | 3.336 |

This demonstrates presentation above 30 FPS with VSync requested. It does not
establish an isolated performance gain. The bypass blocks still include large
stalls (maximum 412 and 301 ms); frame pacing is not fixed.

A separate read-only three-second clock sample with the bypass enabled
recorded 111 update-counter increments in 2.993199 wall seconds (37.084 Hz),
3.002684 seconds of accumulated game time, and 111 presents between its QPC
endpoints. Sampling is non-atomic. This supports wall-time clock progression
above 30 FPS, without validating every gameplay, physics, animation or network
system at higher rates.

Artifacts: `local/guest-pacer-vsync-{windows.json,presents.csv,processes.jsonl}`,
`local/guest-pacer-vsync-contended-report.json`,
`local/guest-pacer-vsync-clock.json`, `local/native-frame-settings-build-tests.log`,
and `local/native-frame-settings-graphics-test.log`.

Consistent carrier pacing, broad higher-rate gameplay timing and an isolated
native triplet-copy comparison remain outstanding; the full goal is active.
