# Native texture reuse across guest addresses

SDK `41c8371` replaces repeated non-cubemap BC texture allocations with bounded
native content reuse. The preceding miss trace found thousands of guest
descriptors for fewer than 100 layout/content groups per measured sweep.
Sharing by native upload contents removes guest-address relocation from the
allocation decision. This is implemented on the experimental branch, without
a new user-facing option; it has not yet been promoted into the delivered WIP.

## Ownership and correctness

The renderer snapshots every mip's bytes before lookup. Matching requires
format, mip count, exact dimensions/row layout, size, and byte-for-byte equality.
XXH3 selects candidates but cannot authorize reuse alone. A miss uploads from
that same immutable snapshot; a hit records no new texture upload. Changed
data gets a separate resource. Per-guest-descriptor hashes, write tracking and
SRV mappings remain independent, including compressed remaps.

Each cache entry owns one COM reference and each returned binding owns another.
Eviction drops only the content cache's reference. Existing guest-cache
retirement fences keep resources used by earlier draws alive. No shared
resource is updated in place. Cube, vertex, surface and ordinary RGBA uploads
retain their existing paths. BC fallback from an override can reuse matching
BC contents while override selection/revision semantics remain unchanged.

Limits are 256 entries, 64 MiB of persistent CPU storage including scratch,
and 64 MiB of retained GPU allocation size obtained from the device. Temporary
allocator storage during a moving `realloc` can exceed the persistent CPU
accounting. Active guest bindings can retain resources after content-cache
eviction, so this limit is not a cap on all renderer GPU memory.

The generic 2D uploader now preflights the entire mip chain and upload-ring
capacity before creating resources or recording copies. Previously a full
ring could return a partially uploaded resource. Failed preparation now
returns null without advancing the upload cursor or caching partial contents.

## Validation

The complete ordinary WARP suite passes. New GPU checks cover relocated
identical bytes, changed bytes, mip/format/layout distinctions, forced hash
collision rejection, caller references surviving clear and LRU eviction,
upload-capacity failure/retry, and actual GPU bytes after clear/eviction/retry.
Existing override, write-watch, geometry, shadow/depth, custom-resolution and
postprocessing checks also pass. The new tests use a private fence timeline.
Initial test runs exposed a mistaken expectation for a valid BC4 hit and a
shared test-fence timeline; both fixtures were corrected before the passing run.

The full profiling candidate builds and runs. Binary SHA-256:
`F5283B6B05950E2BB438100136B8374DDADAC021B34562705800CAC7D9F0753C`.

## First checkpoint capture

Run `20261013-070223-134`, owned PID 50160, uses a fresh private copy of the
confirmed jungle save, 1440p/8192 shadows, VSync off, no frame cap, and muted
real audio. Screenshots establish the tutorial and its dismissal before two
24-second camera sweeps. The game closes normally after measurement.

| Metric | Sweep 1 | Sweep 2 |
| --- | ---: | ---: |
| Eligible native-cache requests | 2,639 | 2,299 |
| Exact-content hits | 2,638 | 2,299 |
| Native misses / actual resource creations | 1 | 0 |
| Hit rate | 99.962% | 100% |
| Mean complete texture preparation | 0.0299 ms | 0.0372 ms |
| Mean resource creation | 0.000410 ms | 0 ms |
| Content-cache evictions during window | 0 | 0 |

Together, 4,937 of 4,938 eligible requests reuse existing GPU storage. Earlier
captures averaged approximately 0.94 ms in the texture preparation scope and
0.88 ms in allocation. Host activity differs, so this is not an isolated
whole-frame speedup claim. The direct request/hit and allocation counts do
establish that repeated native uploads have been removed in these windows.

Both windows retain 250 entries: 62,130,704 bytes of CPU snapshots plus scratch
and 67,108,864 bytes of GPU allocation. Every retained counter row satisfies
the entry/byte limits, monotonicity and requests=hits+misses+bypasses. All
16,094 renderer/content rows and 9,854 descriptor-miss events are retained;
recorders reject zero rows. Miss-to-renderer frame reconciliation and texture
phase reconstruction pass. There is no reported D3D failure.

The strict process-isolation audit fails because another Haze overlaps. The
preserved diagnostic report gives 44.43/44.55 FPS, mean 22.506/22.448 ms,
p95 38.042/31.833 ms, p99 54.461/49.695 ms, maximum 287.429/70.445 ms. These
numbers do not establish an overall improvement or regression. Mean pump wait
is 14.58/14.75 ms and remains a larger outstanding cost than texture work.
No build or screenshot from this agent runs during the timed windows.

## Repeat and remaining work

Configuration audit: boot logs for all six captures from the allocation,
capacity, identity and content-reuse investigations confirm 2560x1440,
8192 shadows, VSync off and no cap. Earlier document/scenario text incorrectly
carried forward native-shadow/VSync-on labels. The reusable launch helper had
read the editable WIP configuration. Logs, not those labels, are authoritative;
the numerical captures are unchanged. Future launches use a separate frozen
benchmark configuration and record its hash, preventing this dependency on
the interactive WIP settings.

Run `20261013-071039-264` repeats the unchanged candidate with another fresh
save copy. No other Haze was running at launch, but another instance starts
during setup and overlaps both timed windows. The strict isolation audit is
again preserved as a failure. The repeat is useful for structural validation,
not an isolated FPS comparison. Diagnostic rates are 52.77/47.88 FPS, mean
18.952/20.885 ms, p95 25.234/28.529 ms, p99 41.038/47.600 ms and maximum
60.267/84.959 ms.

The two windows contain 3,785/3,354 eligible requests and 3,784/3,353 hits:
one new resource in each window, with no content-cache evictions. Mean texture
preparation is 0.0287/0.0302 ms. Across all four windows, 12,074 of 12,077
eligible requests reuse storage (99.975%). CPU storage including scratch stays
below 59,849,488 bytes and retained GPU storage below 64,225,280 bytes in the
repeat's windows. All retained 32,768 counter rows pass bounds and consistency
checks, all 15,923 miss events are retained, and renderer reconciliation
passes. The ring wraps intentionally with zero rejected records.

The last accepted isolated baseline remains the unchanged-binary recovery at
66-69 FPS. Stable 144 FPS and consistent frame pacing are still unachieved.
An isolated comparison and ordinary-build packaging remain before WIP
promotion; the implementation and validation milestone are tracked in #59.

Private evidence: `local/texture-content-cache-{build,tests}.log`,
`local/jungle-texture-content-*` (including the repeat). No game data or binaries
are committed.
