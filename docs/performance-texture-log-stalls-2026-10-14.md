# Texture diagnostic output stalls rendering

Tracking: #71, #59 and #32. This capture identifies a concrete synchronous
logging path; it does not establish the FPS gain from a fix.

## Run and measured windows

Candidate `texture-hash-reasons`, Haze `b671053` / SDK `dabb8b4`, SHA-256
`E2FE060A3BE06D0343AA1263C616BFFCA4370B2883B07DD161A4431699A2A458`,
122,775,552 bytes. The two-job build completes before launch; identity and build
log are retained under `local/texture-hash-reasons-*`.

Muted run `20261014-001154-680`, PID 58396, uses the private real-save jungle
fixture and copied shader cache, 2560x1440, 8192 shadows, uncapped/VSync off.
Fixed CPU 13571, GPU pull 2, VP burst 1 and worker reuse 1 are unchanged;
native job wait, inline read/coherency, bulk fill and milestones are off.
Deferred slow-renderer diagnostics are enabled. The settled rifle scene is
checked after Nectar effects end, then PAD_FILE is detached. Two 45-second
camera sweeps use tokens 3600/3601.

All 7,543 measured intervals are retained: **83.821 FPS / 11.930 ms mean**,
15.193 ms p95, 17.589 ms p99, 28.351 ms maximum. Neither window has a frame
above 33.333 ms. Individual windows average 83.940 and 83.703 FPS. Mean pump
scopes are 6.060 ms wait, 3.945 ms FIFO, 1.841 ms present and 0.084 ms other.

Another Haze PID 55012 overlaps, so strict isolation fails. No compiler overlaps;
20 resource samples have a 5.343-second maximum gap, memory 52.38-52.78%.
No owned compile, screenshot or heavy analysis runs during timing. Added clocks
and host/scene variation preclude a causal FPS comparison. The game exits
normally and original save hashes match.

## Large pre-window stalls are diagnostic prints

All 63 slow callbacks are retained without overwrite; the new schema and nested
partitions validate. None fall within the two settled measurement windows.
The records before measurement show these cache-miss calls:

| Callback / renderer frame | Texture call ms | Direct diagnostic log ms | Hash / decode ms | Containing pump frame / total ms |
| --- | ---: | ---: | ---: | --- |
| 3229062 / 42992 | 382.5533 | 382.5168 | 0.0054 / 0.0114 | 42990 / 397.8248 |
| 3222021 / 42975 | 173.9171 | 173.8240 | 0.0214 / 0.0448 | 42973 / 189.1459 |
| 3222348 / 42976 | 54.8819 | 54.8524 | 0.0035 / 0.0059 | 42974 / 85.1404 |

All three are successful decodes on a full texture cache. The corresponding
source branch prints `[texture-cache] FULL cap=4096 lru-evictions=...` via
`fprintf(stderr, ...)`. The log includes the first 16 full-cache events and
subsequent powers of two. Whole-callback raw thread-cycle deltas are only
488,800 / 817,800 / 380,230; they are not CPU milliseconds. The exact blocking
mechanism (shared CRT stream lock, output backpressure, scheduling, or sink)
is not identified, but the measured intervals are inside synchronous output
on the renderer. No decoding or hashing replacement can eliminate this call.

QPC containment, rather than equal frame counters, links these callbacks to
pump intervals. Their FIFO scopes are 387.1657 / 179.9583 / 76.5745 ms. Frame
42974 contains further direct-log intervals of 6.1962, 5.3569 and 5.2947 ms.
These are real pre-window hitches, not excluded bad frames from the settled
benchmark. Resource observations bracket the sweeps, not these earlier events.

## Hash recurrence and next action

A separate pre-window cache hit at renderer frame 49738 takes 4.1712 ms in the
hash scope, with 363,310 valid raw hash thread cycles. Its skip rejection mask
is exactly 8: write tracking and the prior baseline are valid, hash kinds match,
and the page stamp changed. Thus this full check follows tracked writes rather
than a missing baseline or disabled tracker. It does not prove 4 ms of hash CPU
computation; no hash optimization is promoted from this observation.

The next implementation defers routine texture cache/refresh/decode-failure
diagnostic records to shutdown with bounded storage and explicit overflow,
preserving counters and event data without synchronous formatting or output
from texture binding. Audit/race modes remain explicit diagnostics. Validation
must include exact-source recording/flush checks and a real checkpoint run.

## Implemented replacement

Haze `a11df11` pins SDK `f6cba5c`. Routine full-cache, decode-failure and refresh
messages now append fixed-size records to a 256-event ring. The original
throttling/count values and event frame, descriptor and slot are preserved;
aggregate counters remain complete even if old records are overwritten. The
shutdown summary explicitly reports total, retained and overwritten events.
Recording does no formatting, clock query, allocation or output. Normal window
exit and renderer shutdown invoke the one-shot dump. Explicit audit/race
diagnostics retain their existing behavior.

The extracted actual-source ring/normal-exit-wrapper fixture passes bounded
overflow, ordering, event identity, no recording-time I/O and duplicate-dump
checks. The full WARP live-texture suite exercises actual eviction, failure and
refresh branches, verifies their messages are absent before the dump, then
checks event ordering and exact summary totals afterward. Build-script `-Test`
includes the new regression fixture. A live game run remains a separate check.

## Live replacement validation

Candidate `deferred-texture-logs` builds successfully, SHA-256
`FAEAF863B41475F3206A5B3E1A7BA0B341F95797223E0BDC23704FA4B4A430CF`,
122,776,576 bytes. Run `20261014-002346-576`, PID 9756, uses the same private
fixture/config/cache starting point and diagnostic controls. The settled scene
is checked before PAD_FILE detach and two 45-second sweeps, tokens 3620/3621.

A saved pre-timing log check finds no full-cache, decode-failure, refresh or
texture-summary messages. Normal close emits one summary and all **102 queued
events, without overwrite**: 26 throttled full-cache records and 76 refresh
records. Complete counters report **18,687 full-cache events, 3,269 refreshes
and zero decode failures**. The first full-cache record belongs to frame 43920,
well before the sweeps. The retained event order and counts validate. All 57
slow-draw records are retained without overwrite and have zero direct texture
log time; their maximum callback is 16.263 ms, in pipeline preparation. Original
save hashes match and exit code is zero.

All 7,222 measured intervals: **80.266 FPS / 12.459 ms mean**, 15.844 ms p95,
18.352 ms p99, 25.356 ms maximum, none above 33.333 ms. Individual windows
average 80.346 / 80.187 FPS. Pump means: 6.218 ms wait, 4.272 ms FIFO,
1.877 ms present and 0.091 ms other. Another Haze PID 11976 overlaps, so strict
isolation fails; no compiler overlaps. Twenty resource samples have a maximum
5.374-second gap, memory 52.33-52.78%. No owned builds/screenshots/heavy analysis
overlap measurement. This does not prove an average FPS gain or regression.

The post-close pump ring retains frames 45018-61401 and therefore does not
cover the first full-cache burst at 43920. Its maximum also includes an earlier
pre-measurement presentation interval during scene-capture preparation. Do not
use that ring as a complete, unperturbed startup before/after comparison.
The established result is removal of synchronous output from these confirmed
renderer paths, with event data retained; consistently paced frames and the
remaining producer/command waits are still open.

Evidence prefix: `local/jungle-deferred-texture-logs-a1`, including
`-before-close.json`, `-deferred-validation.json`, diagnostic reports and failed
isolation gates. Build/identity records use `local/deferred-texture-logs-*`.

A local interactive WIP is packaged at
`local/releases/haze-wip-2026-10-14-deferred-texture-logs/Play-Haze.cmd` with the
same executable hash. It preserves the previous WIP's graphics.ini, uses normal
saves/input/audio and fixed performance policy, and clears benchmark controls
and deferred diagnostic CSV paths. PowerShell syntax and binary identity are
checked; the old WIP is preserved. The live validation above exercises this
binary through the muted private-fixture runner, not an unmuted launcher test.

Evidence prefix: `local/jungle-texture-hash-reasons-a1`, including raw draw,
FIFO/flush, pump/present/camera records; windows and process/resource observations;
comparison/slow-draw reports; and `-logging-attribution.json` with full callback
records and QPC-containing pump intervals. No assets or generated code are
committed.
