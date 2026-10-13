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

## Capacity experiment: not promoted

Resource creation accounts for about 93% of texture time. Of 8,418 creations,
7,344 (87%) follow cache misses/evictions. Refresh-only reuse would miss most
of the measured creation volume. Run `20261013-062412-424` tests an 8,192-entry
cache with 16,384 hash buckets, preserving the load factor. Those are the only
behavior changes relative to SDK `54b3fde`. The candidate builds and the normal
WARP/cache regression suite passes. Binary SHA-256:
`3265974436CC5E5A0E134BA5FDCFD38F589C43174583C8C302D0E0127D0B8748`.

The same private checkpoint and tutorial-dismissed camera sweeps produce:

| Diagnostic metric | Sweep 1 | Sweep 2 |
| --- | ---: | ---: |
| Texture creations | 3,255 | 3,119 |
| Descriptor-cache misses | 2,890 | 2,578 |
| Evictions | 0 | 2,468 |
| Content refreshes | 365 | 541 |
| Mean resource creation | 0.822 ms | 0.764 ms |
| Mean texture decode scope | 0.882 ms | 0.826 ms |
| Mean FPS, with process overlap | 61.561 | 61.123 |
| Mean present interval | 16.244 ms | 16.360 ms |
| p95 | 21.669 ms | 21.147 ms |
| p99 | 31.215 ms | 28.825 ms |
| Maximum present interval | 117.143 ms | 50.910 ms |

The strict process-isolation audit fails: another Haze process (PID 58116,
`audio-balance`) starts during the first window and overlaps the second.
The failed report is preserved; the separate `nonisolation-report` retains
mode, consecutive-present and actual-camera checks. These FPS figures cannot
establish a capacity-related gain or regression. All 31,411 renderer rows pass
the phase/call-count checks and the recorder reports zero rejected records.

The cache reaches 8,192 entries and resumes eviction during the second sweep.
Even before any eviction in the first sweep, it makes 2,890 descriptor-cache
miss allocations. Capacity alone therefore does not eliminate the observed
allocation stream. Total creations remain 6,374 over 48 measured seconds.
Miss identity/reuse distance needs attribution before increasing capacity
again; the current evidence does not distinguish streaming descriptors from
avoidable cache-identity fragmentation.

Nine resource samples inside the measured windows show 43.4-45.0% physical
memory use and 0-889 page-ins/sec. The owned process's dedicated GPU allocation
ranges from 2,903,777,280 to 3,568,029,696 bytes (about 2.70-3.32 GiB). Its resident
working set is only 0.81-0.88 GiB, illustrating why that process-list figure
does not describe its GPU allocations or private commitment. GPU accounting
was added after the 4,096-entry run, so no measured GPU-memory delta is claimed.

The two cache constants are restored to 4,096/8,192; the experimental binary
and evidence remain private for reproducibility. No capacity increase is
promoted. Next work in #59 should target descriptor miss attribution and bounded,
fence-safe native resource reuse. The 144-FPS/consistent-pacing goal remains
unmet; this experiment does not change the delivered WIP.

Private evidence: `local/jungle-texture-{phases,recovery}-*` CSVs, windows,
analyses, process/resource logs and requested captures; build/test logs use
`local/texture-phases-*`. Capacity evidence uses `local/jungle-cache-8192-*`
and `local/texture-cache-8192-*`. No assets, private saves or binaries are committed.
