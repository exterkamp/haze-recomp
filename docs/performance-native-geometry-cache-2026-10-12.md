# Native prepared-geometry cache experiment — 2026-10-12

Follow-up to [native texture tracking](performance-native-texture-watch-2026-10-12.md).
The objective remains consistent frame pacing and an unlocked framerate with a
VSync option. A 144 FPS target allows 6.94 ms per frame; this experiment does not
establish that target or stable 30 FPS.

## Question and measurement method

Can a native cache eliminate reference gathering, index remapping, and vertex
conversion for unchanged draws, rather than only cache decoded vertex bytes?
The user permits native replacements, game improvements, and use of additional
CPU/GPU compute. This experiment measures reusable work before choosing a
parallel or GPU implementation for work that actually changes every frame.

All gameplay tests use private copies of the same carrier save fixture, muted
audio with normal audio-device timing, 2560×1440 rendering, 1280×720 window,
AA off, AF 1, shadows following render resolution, VSync on, and frame limit 0.
Native FIFO acknowledgement waits, SPURS queue waits, SPU idle waits, and the
paired native cursor kernel stay enabled. `RSX_CPU_WORK_FILE` stays at 5379,
including the previously tested regional texture tracking; `HAZE_TEXTURE_WRITE_WATCH=1`
registers the demand-committed guest arena. The SINT16 and VP metadata cache
experiments remain off. Profiling runs use `RSX_PROFILE_LIGHT=1` to exclude the
known diagnostic full-table shader scan.

For CPU-stage comparisons, change only the cache control file with
`scripts/benchmark_cpu_work.py --mode-column vertex_cache|geometry_cache`,
four 180-frame blocks in order 0/1/1/0, at least 80 settled warmup frames per
block. Capture/view gameplay before and after; do not capture, compile, or sample
threads during measured blocks. Stage scopes are nested and cannot be added to
derive a frame total. Diagnostic FPS is not ordinary-build FPS.

For an ordinary-build comparison, use successful-present QPC intervals with
`scripts/benchmark_present_modes.py record --mode-column geometry_cache`,
0/1/1/0, 5-second warmup and 45-second measured windows. Close only the owned
instance normally to flush the fixed present ring, then `report` the CSV against
the exact recorded windows. Read-only process observation records other agents'
launches without interfering with them. Contended runs remain evidence with an
explicit limitation, rather than being represented as isolated FPS gains.

## Existing decoded-payload cache probe

Run `20261012-200844-576`, diagnostic candidate SHA256
`0FFA8779C436D50D714B5A083E42CDC1B7F3D23B9050374169D53F266773A8C3`.
Viewed captures 008210 and 010051 show the stationary carrier scene. NPCs and
aircraft still animate. Process observation brackets all four measured blocks
and records only the owned Haze instance.

| Cache | Frames | Pack ms | Refs ms | Remap ms | Decode ms | Hits/frame | Misses/frame |
|---|---|---:|---:|---:|---:|---:|---:|
| off | 9041–9220 | 7.671 | 1.312 | 2.110 | 3.806 | 0 | 0 |
| on | 9321–9500 | 7.164 | 1.343 | 2.105 | 3.278 | 1150.48 | 110.67 |
| on | 9584–9763 | 7.234 | 1.333 | 2.181 | 3.316 | 914.39 | 117.91 |
| off | 9861–10040 | 7.771 | 1.326 | 2.124 | 3.900 | 0 | 0 |

About 75–80% of draws hit, yet decode saves only approximately 0.5–0.6 ms.
Gather/remap still costs about 3.5 ms. The cache compares approximately 23 MB of
source bytes per enabled frame and retains 73–87 MB of decoded payload. Draw hit
rate alone does not establish the share of expensive vertices reused. These
results motivated caching the earlier preparation stages with native page
generations rather than another full source comparison.

Private evidence: `local/vertex-cache-probe-{run,blocks,summary}.json`, renderer
and present CSVs, and process-observation JSONL.

## Prepared-geometry cache contract

`libs/video/rsx_live_geometry_cache.h` is an internal renderer helper. Opt in with
`RSX_GEOMETRY_CACHE=1`; an optional `RSX_GEOMETRY_CACHE_FILE` containing 0/1 allows
settled in-process comparisons. It requires the registered native write tracker
and remains off by default.

The cache retains unique vertex references, the occurrence-to-unique mapping,
primitive-restart cuts, fetch plan, compact layout, and decoded vertices. Hits
skip gathering, remapping, and conversion. They still restore CPU scratch data,
copy vertices into existing transient GPU rings, and build topology indices.
This is not a persistent GPU-resource cache or a GPU conversion implementation.

The key contains shader-used attribute layout, descriptors/defaults, batch
ranges, index address/type, base offsets/index, divider state, primitive/restart
state, relevant CPU conversion options, and resolver identity. Full key equality
follows the hash. Up to eight array and index batches and sixteen attributes are
supported; unsupported spans, inline draws, very small or oversized work, failed
queries, and allocation failure use the existing preparation path.

Source ranges are re-resolved and checked against their original host pointers.
Overlapping/adjacent source pages are merged before querying. Index sources are
stamped **before gathering** and attribute sources **before decoding**. Their
generation baselines stay separate: a later attribute query must not make older
index reads appear current when both share a page. All geometry and texture
queries share one epoch/reset owner. The underlying helper uses combined native
query/reset; Microsoft's [GetWriteWatch contract](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-getwritewatch)
warns that separate query/reset calls can lose intervening writes.

There are 2048 slots, four lookup probes, a 128 MB retained-storage limit, and an
8 MB per-entry limit. Failed decode does not publish an entry. API failure
disables the shared tracker; cache reuse falls back to full preparation.

`RSX_GEOMETRY_CACHE_VALIDATE=1` independently runs full preparation on proposed
hits and compares references, remap, cuts, metadata, and every decoded byte.
It rechecks source epochs after reading. An intervening write counts as an
unstable audit; a stable mismatch disables reuse and keeps full-path output.
Validation deliberately adds work and is not a speed measurement.

## Correctness evidence

The native geometry oracle in `test_live_textures.c` exercises 512 mutation,
format/layout/divisor/restart cases, same-address reuse, disabled-attribute
defaults, 20-bit base wrapping, shared tracker queries, late index writes on a
page also containing attributes, resolver remapping with matching generations,
failed queries, and independent hit auditing. Deliberately corrupting a cached
result must emit a stable-mismatch diagnostic, disable reuse, and retain correct
full-path output; that diagnostic during the oracle is expected. The full
runtime suite passes, including existing vertex, GPU, texture/write-watch, SPU,
FIFO, memory, queue, and savedata checks. Present-window tests also cover both
cache markers.

Live audit run `20261012-202249-986`, diagnostic candidate SHA256
`894A4889D31C9F4BBEF43DA10E5C16E4472B7DA8FEBA5D768F3FD7E760AACF3A`:
**2,532,299 proposed hits audited, zero unstable audits, zero stable mismatches**.
Normal-close summary confirms the same totals and a healthy native tracker.
Viewed captures 008418 and 011526 cover the initial carrier view and movement
toward the squad. This does not validate the entire campaign, prove the reported
squad-conversation slowdown fixed, or establish unlocked simulation correctness.

Private evidence: `local/native-geometry-cache-audit-{run,blocks,summary}.json`
and CSVs. Only the owned instance was closed normally.

## Initial performance regression and diagnosed overhead

Run `20261012-202637-681`, same diagnostic SHA as the audit. Captures 008415 and
012908 were viewed. The shadow agent's `visibility-spu-overflow` instance was
observed throughout the measured blocks, so the overall FPS result is contended.

| Cache | Frames | Pack ms | Refs ms | Remap ms | Decode ms | Native query ms |
|---|---|---:|---:|---:|---:|---:|
| off | 10701–10880 | 7.636 | 1.393 | 2.008 | 3.775 | 0 |
| on | 11001–11180 | 11.020 | 1.037 | 1.594 | 3.662 | 1.333 |
| on | 11265–11444 | 10.658 | 1.049 | 1.594 | 3.596 | 1.271 |
| off | 11541–11720 | 7.754 | 1.394 | 2.052 | 3.862 | 0 |

Gather/remap falls by approximately 0.75 ms but overall preparation regresses by
about 3 ms. This version must not be enabled as a performance default.

Outside all measured windows, a 12-second native instruction/stack sample of
the renderer and producer found repeated `strchr → ld_geom_begin+0x332` stacks.
Object disassembly confirms the return address immediately follows an import
call to `getenv("RSX_NO_RESTART")`, accidentally evaluated for each indexed draw.
The existing fetch path already caches that immutable environment setting.
The revised cache now does the same. Sample counts include waits/suspension and
are not CPU percentages; sampling itself suspended threads for about 984 ms
across 2716 suspensions and is excluded from FPS evidence.

The revision also exits cheaply when disabled and adds separate begin, store,
restore-copy, and native-query timers plus hit vertex/reference counts. Begin
includes hit restoration and some queries; query time is also included in other
stages. Do not add them together. The existing CSV `vertices` field is expanded
topology, not unique prepared vertices; dividing hit vertices by it would not
measure the fraction of decode work eliminated.

Private evidence: `local/native-geometry-cache-stage-{run,blocks,summary,cpu}.json`,
CSVs, process log, and `local/native-geometry-cache-begin-asm.txt`.

## Revised cache measurements

Revised diagnostic candidate SHA256
`368FFA50A4406D3B6EC73C6C903889CAD431E601B48D6583CF04FCAAF893756A`,
run `20261012-203832-645`, captures 008923 and 011139 viewed. Full runtime tests
and the three present-window test methods pass. Another agent's
`visibility-decision-trace` Haze instance was observed during this run.

| Cache | Frames | Pack ms | Refs ms | Remap ms | Decode ms | Begin ms | Store ms | Query ms | Restore-copy ms |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| off | 9621–9800 | 9.630 | 2.101 | 2.324 | 4.562 | 0.033 | 0.032 | 0 | 0 |
| on | 9921–10100 | 10.772 | 1.759 | 1.989 | 4.543 | 1.692 | 0.460 | 1.378 | 0.341 |
| on | 10182–10361 | 11.028 | 1.703 | 1.941 | 4.682 | 1.885 | 0.484 | 1.486 | 0.389 |
| off | 10461–10640 | 10.326 | 2.233 | 2.409 | 5.031 | 0.033 | 0.031 | 0 | 0 |

Enabled blocks reuse 597.79/711.03 draws, 83,912/95,051 unique vertices, and
172,782/199,656 source references per frame. They make 2736/2973 native source
queries per frame and retain approximately 25–26 MB. The cache avoids some
gather/remap work, but query/storage/restore costs still erase the benefit in
these measured scopes. The stationary scene's expensive decode work is largely
left over. No cache audit is active in these timing windows.

Diagnostic mean frame times drift 50.056 → 55.163 → 56.011 → 73.346 ms, including
a 337.63 ms final-block maximum. This run does not establish a clean total-frame
speed comparison or causally attribute that drift to either Haze process.
Private evidence: `local/native-geometry-cache-cached-config-stage-{run,blocks,summary}.json`,
renderer/present CSVs, and process JSONL.

Ordinary candidate SHA256
`5D2DAA5B6989088DA11EC098F23CC05D2DA7B9CD3315FE59A95A77B2183CCBF8`,
run `20261012-204233-223`, captures 008508 and 020288 viewed. The owned process
closed normally, exit 0. Its native tracker stayed healthy and the geometry
cache reported no failure. Both comparison sequences use this same ordinary
binary and stationary camera; animated scene content still drifts.

Initial sequence, 45-second windows, 5-second warmup:

| Cache | Frames | FPS | Mean ms | p95 ms | Max ms |
|---|---|---:|---:|---:|---:|
| off | 9602–10857 | 27.945 | 35.785 | 52.538 | 257.198 |
| on | 10997–12271 | 28.355 | 35.268 | 49.160 | 69.134 |
| on | 12409–13624 | 27.054 | 36.963 | 53.376 | 248.307 |
| off | 13762–14854 | 24.305 | 41.144 | 59.155 | 87.231 |

The process observer records `visibility-decision-trace` during the initial
sequence, then its exit. This sequence does not establish an isolated cache
gain. All windows and spikes remain in the report.

Reversed sequence, 30-second windows, 5-second warmup:

| Cache | Frames | FPS | Mean ms | p95 ms | Max ms |
|---|---|---:|---:|---:|---:|
| on | 15627–16457 | 27.727 | 36.066 | 50.554 | 92.536 |
| off | 16594–17436 | 28.145 | 35.530 | 50.804 | 71.383 |
| off | 17576–18370 | 26.543 | 37.674 | 54.933 | 74.226 |
| on | 18510–19340 | 27.746 | 36.041 | 51.027 | 67.010 |

Only the owned Haze was observed throughout the reversed windows; coverage
brackets every window with 16/16/16/17 samples and maximum gaps
2.104/2.153/2.144/2.113 seconds. Other applications and launches between samples
are not excluded. Equal-block mean frame time is 36.602 ms off versus 36.053 ms
on, corresponding to 27.321 versus 27.737 FPS, approximately 1.5% nominal gain.
The enabled mode loses the first adjacent comparison and wins the later one.
That small mixed result does not establish a repeatable net improvement.
Frame pacing remains inconsistent; stable 30 FPS and 144 FPS are not achieved.
**Keep the geometry cache off by default.**

Private evidence: `local/native-geometry-cache-cached-config-ordinary-{run,windows,report}.json`,
`local/native-geometry-cache-cached-config-ordinary-reverse-{windows,report}.json`,
their process JSONL files, and the shared ordinary present CSV. Reports validate
complete QPC coverage, consecutive positive successful-present intervals, and
settled cache mode throughout each measured window. No intervals are trimmed
for being slow. Both owned benchmark processes exited normally and audio was
muted throughout.

## Next architectural decisions

Persistent native vertex/index buffers could remove the remaining scratch/ring
copies on a valid hit. Large changing meshes instead need native optimized
conversion, queued CPU preparation, or GPU conversion/vertex pulling. Any GPU
approach must measure endian/format conversion, source uploads, ordering,
resource lifetime, and synchronization in the full frame. More workers alone
cannot shorten serial state interpretation or the command producer's critical
path. Game-level changes such as avoiding redundant mesh preparation or changing
update frequency are authorized, but require a concrete measured workload and
visual/behavior checks before adoption.
