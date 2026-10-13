# Native vertex-program packet preparation

Tracked in [#35](https://github.com/zdiemer/haze-recomp/issues/35) and
[#38](https://github.com/zdiemer/haze-recomp/issues/38), with attribution in #9.
The guarded producer hook is implemented, passes its boundary tests, and runs
on the carrier. Its first ordinary comparison does not establish a performance
gain. It defaults off. Direct native draw submission remains a later step under #35.

## Caller evidence

The ordinary native-index candidate (SHA256
`54802A22F6528D704FE5308916C7BFC44FBECE4BD049DFCEEC394C28CB3D7F07`, SDK
`ca743b0`, root implementation `471e590`) ran muted at 1440p/follow shadows,
VSync on, uncapped, GPU occurrence mode 2, CPU flags 13571, vertex/geometry/VP
caches and vertex workers off, and native job wait off. Run
`20261013-015447-393` used private saves/input. Capture 011965 verifies the
carrier before sampling.

A three-second thread-counter scan selected four active threads. A separate
16.006-second sample recorded 1,482 rounds, stacks every fourth round at depth
20, and up to 400 distinct chains per thread. These diagnostic suspensions
are outside every FPS window. Their 5,928 suspensions total 1,325.17 ms
(maximum 3.13 ms); sampled locations include waiting threads and are not CPU
utilization shares or a per-frame timing breakdown.

| Host thread | Identified work | Measured CPU ms |
| --- | --- | ---: |
| 66552 | Guest main thread 1 | 12937.500 |
| 38984 | Guest thread 8, event wait and timer polling through `00B73738` | 9609.375 |
| 8368 | Renderer / FIFO consumption | 6140.625 |
| 17556 | SPU image 26 and audio code | 5359.375 |

Process CPU is 82140.625 ms; only 34046.875 ms is attributed to these four
selected threads. Other threads were intentionally not sampled. Do not assign
the remainder to one subsystem. The main-thread stacks retain active callers
previously hidden behind the most frequent waiting chains. `0072C368` appears
as the nearest game caller in 24 recorded stack samples; seven have a memory
or reservation helper at the top. `0072BAD0` appears in twelve, six through
helpers. These counts nominate operations for inspection, not an estimated
saving. The renderer still reaches FIFO interpretation, program hashing and
real waits.

Private diagnostic: `local/native-index-refs-cpu-callers-sample.json` (UTF-16),
thread-counter JSON, run log and captures. The native-index mode change occurs
at frame 13260 before sampling; GPU preparation reports zero bypasses at exit.

## Audited packing loop

Inspection of `0072C368` identifies vertex-program upload packet production.
The full-block loop at `0072C4F8` emits header `0x00800B80`, followed by 128
bytes of already big-endian program data, and advances the command-context
cursor by 132 bytes. Each block performs 35 `vm_read32` and 34 `vm_write32`
calls, including three cursor reads and one cursor update. The source payload
is converted to host integers only to be converted back on each store.

`port/haze_vp_packets.h` replaces that payload operation with a host-span loop:
emit the four header bytes and copy 128 bytes. It contains no guest context,
guest registers, endian round trips, memory-helper calls or command submission.
It checks multiplication/capacity/address overflow and rejects overlapping
source/output spans before writing. Zero blocks is an empty operation. The
caller must own the spans until publication; the kernel neither establishes
ownership nor writes any guest cursor.

`extract_vp_packet_oracle.py` verifies the entire private function hash and the
exact loop hash before extracting the original, unchanged loop to an ignored
local include. `test_vp_packets.cpp` compares full fixture memory after packing
for counts 1..64 and every source/output byte alignment 0..15: **16,384
comparisons pass**. It separately checks empty input, insufficient capacities,
null inputs, overlap rejection without writes, oversized counts and Windows
guard pages immediately after exact source/output spans. Oracle extraction
and changed-source rejection tests pass. The focused C++ test builds with
clang-cl `/O2 /W4 /std:c++17` and is included in the standard `build.ps1 -Test`
gate. These kernel tests alone do not establish a runtime speedup.

## Runtime integration work

The build-local patch composes after the guest-pacer and job-wait patches,
guarded by the complete `0072C368` function hash. It tries the native operation
once before entering the full-block loop; fallback backedges do not retry the
hook. Both routes join before the unchanged tail/remainder path. Allocation
and capacity callbacks execute before the boundary, as in the original.

The adapter accepts only main guest thread 1, TOC `E94EE0`, the expected command
header, matching original source/count registers, 1..64 blocks and aligned,
nonwrapping spans that fit the actual context end. The context must match the
title's current GCM context and use the installed synchronized wrap callback.
The FIFO walker consumes the separately published PUT, not the allocation
cursor. Its heuristic recycling is disabled for this callback; the callback
continues to perform the original wait/wrap when the producer needs more room.
The game retains responsibility for serializing its command producers.

SDK `ppu_native_memory_begin` checks ordinary RAM, VM bounds, the relocatable
HLE control/label/table window, size limits and disjoint source/output/context
spans. It rejects enabled memory probes or barrier watches. It then holds the
SPU reservation lock while checking every covered 128-byte line and performing
the native copy. Any reserved line rejects the operation; a new reservation
cannot appear between validation and copying. The source is the already
available shader payload; no new job completion or source readiness is assumed.
No services, waits, allocation or logging occur inside the acquired scope.
Ordinary host stores continue to participate in the existing OS write-watch.

The adapter rechecks context bounds/current under that scope, emits the payload,
and updates the allocation cursor after a release fence. Later FIFO publication
and its ordering remain in the original code. It also reconstructs the exact
final values of the loop's modified registers and CTR, including scratch values;
all other context state is untouched. The original path handles rejected cases.

An additional **1,024 comparisons** against the exact extracted loop verify
the entire guest-memory fixture and complete `ppu_context` at the join point
(1..64 blocks, all four word alignments on each side). The test links the actual
adapter and SDK scope implementation. Stub reservation functions assert that
checks run with the lock held and all exits release it. Mutation cases verify
unchanged memory/context on rejection, including source, last-output-line and
context reservations. Range tests cover low memory, wrapping, hardware windows,
relocated HLE overlap and exact VM bounds. Patch tests verify reversibility,
composition and refusal of changed or already patched input.

The full `build.ps1 -SpuFma -Test` gate passes. Candidate
`local/candidates/native-vp-packets/HazeRecomp.exe` has SHA256
`D30B78B5DA44D7299F228A5894838D0998486783E17083C2668312F57022FF4E`.
The internal `HAZE_NATIVE_VP_PACKETS` / `HAZE_VP_PACKET_FILE` selectors default
off; no graphics setting is added. Mode events and counters expose acceptance
and rejection. Optional `HAZE_VP_PACKET_PROFILE` times both complete loop routes
for a separate diagnostic, outside ordinary FPS windows.

Next, measure live acceptance/call/byte volume and elapsed cost, then compare
ordinary camera sweeps and squad progression. Neither this contract fixture
nor the host kernel oracle proves a whole-frame gain or campaign correctness.

## First ordinary runtime comparison

Run `20261013-022938-078`, owned PID 31188, used the candidate above with real
audio muted, private saves/input, 2560x1440 rendering, follow-resolution shadows,
AA off, AF 1, VSync on, no FPS cap, GPU occurrence mode 2 and native index
preparation enabled. Vertex/geometry/VP caches, vertex workers and native job
wait remained off. Capture `requested_011668.ppm` confirms the carrier scene.
Before recording, a mode change reported 1,654,321 accepted native calls,
11,252,470 packed blocks and zero rejections. This confirms runtime use,
not a performance improvement.

Four 24-second windows, each preceded by five warmup seconds, repeated the
same absolute camera sweep (30 degrees either side, four-second period).
No profiling, captures or builds occurred inside these windows. All present,
mode, actual-angle and process-coverage checks pass. Only the owned Haze was
observed, with maximum sampling gaps below 2.232 seconds. The window rectangle
remained unchanged. These observations do not exclude other applications.

| Native upload | Frames | FPS | Mean ms | p95 ms | p99 ms | Maximum ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Off | 533 | 22.28 | 44.881 | 60.822 | 73.420 | 145.652 |
| On | 543 | 22.64 | 44.174 | 62.289 | 76.961 | 127.587 |
| On | 486 | 20.29 | 49.279 | 77.520 | 104.117 | 145.991 |
| Off | 541 | 22.64 | 44.178 | 57.653 | 75.439 | 181.083 |

Pooled intervals average 44.527 ms off (22.46 FPS) versus 46.585 ms on
(21.47 FPS): the enabled windows are about 4.6% longer in this run. Intervals
above 50 ms are 23.65% off and 30.13% on. There is no demonstrated whole-frame
gain or pacing improvement; do not promote the replacement on these results.
System-wide CPU busy time across the four windows is approximately 82.91%,
82.90%, 83.44% and 77.48%. This differs from earlier sessions and prevents
attributing the lower absolute FPS relative to those sessions to this patch.
Within this run, both routes remain slow. World and NPC state also remain live.

Normal close returned exit 0. Exit diagnostics report zero draw drops, texture
decode failures and GPU geometry bypasses; texture write-watch is healthy.
This is a carrier smoke test, not full campaign correctness. Separate timing
of the replaced loop and a reverse-order confirmation remain pending.

Private evidence: `local/native-vp-packets-motion-{windows,report}.json`,
`-presents.csv`, `-processes.jsonl`, `-camera.txt.csv`, and the run log above.
