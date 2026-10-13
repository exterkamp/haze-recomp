# Experimental retained-worker capacity

SDK commit `65423e1` increases retained host-worker slots from 16 to 32.
This follows the raw dispatch sequence documented in
`performance-idle-worker-five-2026-10-13.md`: twelve image-30 and four image-33
jobs occupied every retained slot before six additional image-30 jobs fell
back to delayed thread creation. Several executions finished together.

Capacity limits only retention/reuse, not simultaneous execution. A full
cache still starts dedicated workers; there is no queue, new concurrency cap,
changed priority, or bypass of job initialization, completion or cleanup.
Exact whole-image eligibility remains the same five workloads. Idle workers
still retire after five seconds. Each retains the existing 256 MiB stack
reservation; additional reserved address space is not equivalent to physical
memory consumption, which must still be measured during live runs.

This is a targeted experimental change, not proof of a pacing gain or a fix
for the separately observed large FIFO stalls. Existing full ordinary-build
regressions pass, including the worker-cache ownership/overflow/nested-progress
tests. Those core tests use a two-slot fixture and do not establish live
32-slot occupancy or whole-game performance.

Candidate: `local/candidates/idle-worker-32-ordinary/HazeRecomp.exe`, SHA256
`37F8F02193F0CACEC97D6AF0D7D2D8BEB8BBEC79756891AF6FA40163286FDB59`.
Built from root `c2972e4` with the SDK change, ordinary profiling-off lane,
native SPU arithmetic/FMA, two compiler jobs. Build/full regression log:
`local/idle-worker-32-ordinary-build.log`.

## Ordinary comparison

Four fresh launches use the same copied jungle checkpoint in order
32/16/16/32. Each has two 24-second absolute 30-degree camera sweeps after
five-second warmups. Rendering is 1440p with 8192 shadows, VSync off/uncapped;
five-job reuse and the native job-0 kernel stay enabled. Tutorial dismissal
and Nectar activation are visually checked before measurement. No stack
sampling, heavy profiling, own builds or screenshots run during measurement.
Audio is muted; every owned game closes normally. All seven baseline save
hashes remain unchanged. Live NPC/world state is not a deterministic replay.

Control: `idle-worker-five-ordinary`, SHA256
`A6ACBC36AE4C5D9022D2750C8A86DCB783754C3A4A4F373331D1CB0FC7D883A7`,
SDK 5108fbe. The capacity default is the only SDK source change in the new
binary; the root sampler changes run outside the game and are not linked in.

| Run | Slots | Intervals | Mean ms | FPS | p95 ms | p99 ms | Max ms | >33.333 ms | >50 ms |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| A1 | 32 | 4194 | 11.438 | 87.429 | 14.613 | 17.313 | 88.011 | 1 | 1 |
| B1 | 16 | 4043 | 11.866 | 84.274 | 14.873 | 17.263 | 35.739 | 1 | 0 |
| B2 | 16 | 3851 | 12.415 | 80.547 | 15.654 | 19.854 | 446.320 | 21 | 15 |
| A2 | 32 | 4276 | 11.219 | 89.134 | 14.406 | 16.864 | 27.024 | 0 | 0 |

All four camera/mode/present/Haze-process audits pass. Resource observations
bracket every comparison with maximum gaps below 5.367 seconds. Only the owned
game appears in those observations. A1/B1/B2 have zero observed compilers;
A2 has one compiler snapshot **5.332 seconds before its first window**, during
warmup, and none inside either measured window. Its exact exit time is unknown.
Memory ranges respectively: 48.14–48.48%, 49.03–49.23%, 49.11–49.63% and
48.40–48.72%. Window rectangles, nonminimized/normal-priority state and sampled
foreground process stay unchanged. Sampling cannot exclude other activity or
short-lived processes between observations.

Pooling every raw measured interval gives 32 slots **11.3274 ms / 88.2815 FPS**,
versus 16 slots **12.1339 ms / 82.4137 FPS**. This is 0.8065 ms / 6.65% lower
mean frame time and 7.12% higher throughput in this comparison. Pooled p95:
14.5069 versus 15.2694 ms; p99: 17.0435 versus 18.3188 ms. Both candidate
launches have lower means than both controls. No stalled interval is removed.
This supports retaining the capacity experiment for further validation; it
does not prove broad gameplay behavior or a reliable reduction in rare stalls.

The candidate's 88.0121 ms pump interval spends 78.3164 ms in presentation,
2.8411 ms in FIFO and 6.8371 ms in outer waits. The control's 446.3193 ms pump
interval instead spends 418.2232 ms in FIFO, 23.7702 ms in outer waits and
2.1181 ms in presentation. These different scopes require separate attribution;
neither proves worker capacity caused or fixed the delay. The 144-FPS budget
of 6.944 ms and consistent pacing remain unmet.

Private evidence: `local/jungle-cache32-a1` (run `20261013-115115-395`, tokens
2280/2281), `local/jungle-cache16-b1` (`20261013-115610-814`, 2300/2301),
`local/jungle-cache16-b2` (`20261013-120059-212`, 2320/2321), and
`local/jungle-cache32-a2` (`20261013-120613-286`, 2340/2341). Each retains raw
presents/pump/camera/process/resource data and validated reports. Combined
raw-interval result: `local/worker-capacity-ordinary-comparison.json`.

Next: verify live dispatch spill/startup changes at 32 slots, investigate the
independent long stalls, and validate broader transitions/output behavior.
Existing user WIP remains unchanged; tracking #36, #32 and #9 remain open.
