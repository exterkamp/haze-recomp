# Texture allocation attribution and host-pressure recovery

The new diagnostic counters split texture decoding into committed-resource
creation, upload memcpy, copy-command recording, barrier recording and a
residual. Calls are classified as descriptor-cache miss, content refresh or
failed-decode retry. Per-present maximum creation duration includes resource
dimensions/format/mip count and call kind. Helpers outside the profiled decoder
are excluded from this partition. No user-facing option is added.

The profiling game build passes. Existing normal-mode WARP texture tests pass,
including mutation/watch, overrides, failed-decode retries, geometry/vertex
oracles and GPU readbacks. An additional attempt to compile that standalone
test with profiling enabled initially had a local wrapper argument error;
after correction it fails to link title-owned symbols pulled from the SDK.
It is not a passing profiling test. Counter validation below uses the complete
profiled game, which links and runs successfully.

## Pressure-related slowdown capture

Run `20261013-060142-028`, candidate `texture-phases`, private snapshot copy,
muted real audio, 1440p/native shadows/VSync on/cap zero. Binary SHA-256:
`C4ED9F240AA28E0C12DD9ED6B6DE30AD1960C4D2151399B074BE231E8E87C693`.

The user reports 94% physical memory usage and several simultaneous compiler
processes while this run collapses to about 1 FPS. Two 24-second diagnostic
windows retain only 22/25 renderer samples. Complete pump intervals average
1084.987/951.130 ms, including 1013.484/885.661 ms waiting. Texture decode and
allocation are zero in those measured windows. This does not reproduce the
earlier 146-ms texture-path spike and is not an accepted gameplay benchmark.

Another Haze from `audio-balance` overlaps this capture. A subsequent process
inspection also finds a `menu-logos` build using `--parallel 12`, with multiple
large PPU compilation processes, and an `audio-balance` build using parallel 8.
The subsequent memory sample records 13,801 MiB available and 1,003 page-ins
per second; it is after the timed windows and does not quantify their faults.
The user's 94% report is not replaced with that later sample. All our builds
had finished before the measured windows. No other agent process was stopped.

## Unchanged-binary recovery

Run `20261013-060901-127` repeats the same binary and private snapshot. The
initial automatic menu sequence misses; screenshots verify Continue/OK before
actual resume and the tutorial-dismissed gameplay scene before timing. Two
24-second camera sweeps pass mode, actual-angle, consecutive-present and
Haze process-isolation audits. No build or screenshot from this agent runs
during those windows. World/NPC state remains live.

| Metric | Sweep 1 | Sweep 2 |
| --- | ---: | ---: |
| Mean FPS | 68.877 | 66.163 |
| Mean interval | 14.519 ms | 15.114 ms |
| p95 | 19.465 ms | 20.212 ms |
| p99 | 25.006 ms | 25.090 ms |
| Maximum interval | 37.027 ms | 40.436 ms |
| Mean texture decode scope | 0.947 ms | 0.937 ms |
| Mean resource creation | 0.884 ms | 0.873 ms |
| Mean upload memcpy | 0.010 ms | 0.011 ms |
| Mean copy-command recording | 0.045 ms | 0.045 ms |
| Mean barrier recording | 0.007 ms | 0.007 ms |
| Mean decoder residual | 0.002 ms | 0.002 ms |
| Texture creations | 4,408 | 4,010 |
| Cache misses / evictions | 3,904 | 3,440 |
| Content refreshes | 504 | 570 |

Renderer samples whose endpoints lie within each window are used for texture
counts/means; complete present intervals are used for FPS. Retained CSV rows
have 119 aligned fields, consecutive frame IDs, finite nonnegative durations,
exact call-kind sums and no recorder rejections. Subphases reconstruct the
outer texture scope within floating-point rounding. Per-frame maximum state
resets correctly on frames with no creations.

The slowest creation in the first window takes 5.545 ms, for a cache miss on a
256x256 BC1 texture with nine mips. That frame's two creations take 8.248 ms of
8.321 ms in the texture path. The second window's maximum individual creation
is 4.430 ms, also a cache miss. The earlier 146-ms event does not recur here.

Nine additional resource observations within these windows report 41.0-42.3%
physical memory use. Sampled page-ins range from 0 to 305/sec. Up to two small
compiler processes appear, with maximum sampled resident size 162.4 MiB;
this is not a claim of zero other host work. Only the owned Haze is observed
during both timed windows. Recovery strongly implicates host pressure in the
temporary 1-FPS collapse but does not isolate paging as the sole cause.

## Next measured intervention

Resource creation accounts for about 93% of texture time. Of 8,418 creations,
7,344 (87%) follow cache misses/evictions. Refresh-only reuse would miss most
of the measured creation volume. First compare the retained 4,096-entry binary
against an 8,192-entry native texture cache, preserving the hash-table load
factor. Measure misses, allocations, frame tails and retained memory. Larger
capacity increases possible GPU residency and doubles the eventual full-cache
LRU scan; it is an experiment, not an assumed improvement. This work is tracked
in #59. The 144-FPS/consistent-pacing goal remains unmet.

Private evidence: `local/jungle-texture-{phases,recovery}-*` CSVs, windows,
analyses, process/resource logs and requested captures; build/test logs use
`local/texture-phases-*`. No assets, private saves or binaries are committed.
