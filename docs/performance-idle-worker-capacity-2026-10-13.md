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

Ordinary comparison against the existing five-workload, 16-slot binary and
live dispatch/spill verification remain pending. Existing user WIP remains
unchanged; tracking #36, #32 and #9 remain open.
