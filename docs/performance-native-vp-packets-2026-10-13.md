# Native vertex-program packet preparation

Tracked in [#35](https://github.com/zdiemer/haze-recomp/issues/35) and
[#38](https://github.com/zdiemer/haze-recomp/issues/38), with attribution in #9.
This is a tested host packing kernel and a selected producer boundary, not an
installed game hook or a demonstrated FPS improvement. Direct native draw
submission remains a later step under #35.

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
gate. No full game rebuild or runtime speedup is claimed for this unused kernel.

## Runtime integration work

Before installing the hook, establish the source lifetime and command-buffer
ownership at this boundary. Audit reserve/callback paths, context cursor versus
published FIFO put, capacity, wrapped addresses, aliasing, MMIO, watched memory
and SPU reservation behavior. A raw host pointer alone proves none of these.
Unsupported cases must execute the original loop without partial native writes.
The full function includes partial blocks and further state updates; retain
their ordering and audit which register values the continuation actually uses.

Compare the integrated boundary against the extracted loop, including cursor,
required continuation state and publication behavior. Measure call/byte volume
and elapsed cost before attributing a whole-frame saving, then compare ordinary
camera sweeps and squad progression. The current test proves the host packing
operation only; the runtime boundary and direct-submission acceptance criteria
remain open.
