# Vertex-program preparation experiment

The earlier carrier profile measured 2.39 ms/frame in vertex-program size scans,
input analysis, TXL analysis and duplicate FNV hashing, with no shader compilation.
Fragment-program preparation was 1.59 ms/frame. These scopes overlap PSO/draw
scopes and must not be added to them.

The experiment caches instruction count, conservative input mask, vertex-texture
unit mask and the exact existing VP hash. Every lookup compares all instruction
bytes through END, including the END bit. A prefix hash only selects a bucket;
it cannot establish a hit. Fixed 64-set, four-way storage holds at most 256
programs of up to 544 instructions. It caches no guest addresses, constants or
render state. Out-of-bound programs fall back to analysis without storage.

Bit 512 in RSX_CPU_WORK_FILE enables it. The normal default remains 259, so this
experiment is disabled in both preserved WIPs pending ordinary-build benchmarks.

## Same-process profiling comparison

Muted, uncapped run `20261012-011728-601`, stationary carrier view toward the
squad. Gameplay was inspected in requested_017227 before measurement. Host depth
clipping stayed enabled throughout these blocks; no captures, builds or CPU
sampling occurred during them. Each block contains all 180 consecutive frames,
after at least 80 warmup frames. No frames are dropped for low draw counts or
slow intervals. All blocks have zero shader compilations.

| Flags | Frames | FPS | p95 ms | VP prep ms | PSO prep ms | Draws/frame |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| 259, previous | 17617-17796 | 21.025 | 63.297 | 2.2361 | 4.3663 | 1585.3 |
| 771, cached | 17901-18080 | 21.319 | 61.061 | 0.2463 | 2.5689 | 1615.2 |
| 259, previous | 18201-18380 | 22.010 | 59.166 | 1.9833 | 3.9232 | 1398.5 |
| 771, cached | 18501-18680 | 21.904 | 60.449 | 0.2228 | 2.4332 | 1442.8 |
| 259, previous | 18801-18980 | 21.002 | 62.908 | 2.1641 | 4.2467 | 1542.3 |
| 771, cached | 19101-19280 | 21.041 | 62.219 | 0.2353 | 2.5255 | 1570.1 |

Cached blocks averaged 3,189/2,845/3,099 verified hits and 41 misses per frame.
The VP scope falls substantially. FPS and p95 changes are small/mixed, NPC draw
counts differ, and texture evictions total 434-586 per block. There is no
established end-to-end FPS or pacing improvement. The new path also executes
fewer profiling clock calls; an ordinary-build comparison is needed to separate
algorithm savings from instrumentation savings.

## Correctness and further measurement

The full normal and profiling suites pass. A separate cache oracle verifies
162,064 byte mutations across randomized and maximum-size programs, explicit
input/TXL/unknown-op metadata, changed END positions, shortened bounds, contents
reused at another address, bucket eviction, oversize fallback and guarded tails.
The original decompiler analysis and independent hash/TXL calculation are the
reference. Native frame CSVs have separate VP/FP scopes and cache hit/miss counts.

Ordinary builds now record the CPU-work mode in the existing fixed present ring,
without per-draw clocks or live CSV writes. `benchmark_present_modes.py record`
records timed mode blocks for an explicitly selected executable/PID. After normal
close flushes the ring, `report` checks settled modes and complete consecutive
intervals. Its fixture retains a deliberate stall and rejects mode mismatches,
missing presents and incomplete recordings. A native normal-build comparison
has not yet run because the user is playing; do not present the fixture or the
profiling blocks as that evidence.

Raw artifacts remain ignored under local: vp-cache-profile-blocks.json,
vp-cache-profile-report.json and the run's renderer.csv/presents.csv.
