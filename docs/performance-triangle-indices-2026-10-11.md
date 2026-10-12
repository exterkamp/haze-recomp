# Triangle-index upload investigation

## Current CPU baseline

Muted profiling run `20261012-041612-400` uses merged controls main `d4cefb3`
and SDK `fa8f304`. Carrier captures 8320 and 9315 bracket the stationary view.
Frames 8500–8799 are 300 consecutive successful presents, cross-checked against
the renderer CSV. No other Haze or compiler process was observed at the window
boundaries, and no instruction sampler, builds, or captures ran within it.
Earlier parts of this run shared the machine with rendering-resolution tests
and are excluded. The process closes normally with exit code zero.

| Measurement | Value |
| --- | ---: |
| Profiling-build FPS | 21.213 |
| Mean / p95 frame time | 47.141 / 62.813 ms |
| Frames above 50 ms | 106 / 300 |
| Draw preparation | 25.105 ms |
| Vertex fetch/pack | 7.657 ms |
| Texture hashing | 3.515 ms |
| Pipeline preparation | 4.310 ms |
| Texture decode | 1.099 ms |
| GPU execution | 1.464 ms |
| Texture evictions | 617 total, 2.057 per frame |
| Frames compiling shaders | 0 |

CPU scopes overlap. This is an instrumented baseline, not a normal-build FPS
claim. In particular, cache eviction counts in earlier investigations are
totals over each block, not hundreds of evictions per individual frame.

Separate instruction sampling resolves the actual candidate's symbols.
The renderer samples include vertex fetch/remap, texture hashing and triangle
index construction. A persistent SPU worker spends 11.828 CPU-seconds in a
12.001-second focused sample, predominantly compiled queue/atomic routines.
Repeated native spin diagnostics identify guest queue line `0x40400180`; the
logged producer/consumer words match. This supports investigating idle polling,
but does not yet prove its cost on the frame's critical path. Sampling includes
waits; RIP fractions are not CPU-utilization percentages. The selected-thread
sample intentionally excludes other threads from its tracked total.

## Opt-in triangle-list experiment

Triangle lists already have a complete occurrence-to-unique map produced by
the validated vertex remapper. Their index upload currently calls an external
bounds-checked accessor once per occurrence. CPU-work bit 1024 instead copies
the complete-triangle prefix when the map is present and large enough. Short
maps retain the accessor path; strips, fans and quads retain their winding and
restart conversion. The initial experiment retained default 259. A new
`topology_index_ms` scope times this work separately.

The production writer passes 2,064 independent ordered-oracle cases covering
duplicate maps, identity fallback, undersized maps, incomplete triangles,
sentinel output bounds and protected source tails. The full rendering gate,
including WARP readbacks, also passes. Benchmark-window validation accepts and
checks CPU-work mode 1283 (259 plus 1024).

An optional standalone benchmark (`LD_TOPOLOGY_BENCH=1` on
`local/test_live_textures.exe`) times 32 repetitions of 1,000,005 identical
indices, checking output equality outside the timed region:

| Pair | Accessor ms | Bulk-copy ms |
| --- | ---: | ---: |
| 1 | 1.239 | 0.138 |
| 2 | 1.490 | 0.135 |
| 3 | 1.348 | 0.134 |

These are synthetic stage costs, not FPS gains.

## Repeated native comparison

Muted profiling run `20261012-042956-862` uses SDK experiment `fe9e69b`.
Captures 4501 and 7831 verify carrier gameplay before/after measurement.
All six blocks use 80 warmup frames followed by 180 measured frames; there
are no captures, builds, instruction sampling or job logs during the blocks.
No other Haze or compiler process appears in the repeated process checks.
The presentation CSV independently verifies consecutive IDs and the CPU-work
mode at both endpoints of all 1,080 intervals. Window-position mode stays one.
The process closes normally with exit code zero.

| Path | Frames | Index ms | Draw CPU ms | FPS | p95 ms | Draws/frame |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| Accessor | 5123–5302 | 1.0896 | 21.924 | 24.399 | 52.601 | 1336.2 |
| Bulk copy | 5421–5600 | 0.2085 | 20.624 | 25.445 | 50.812 | 1260.8 |
| Accessor | 5721–5900 | 1.0608 | 23.546 | 23.927 | 55.482 | 1353.2 |
| Bulk copy | 6021–6200 | 0.2135 | 23.028 | 23.504 | 54.591 | 1405.2 |
| Accessor | 6321–6500 | 1.0607 | 23.025 | 23.485 | 58.862 | 1418.1 |
| Bulk copy | 6621–6800 | 0.2102 | 20.997 | 24.131 | 54.188 | 1423.2 |

Index construction saves 0.847–0.881 ms/frame in all three pairs. In the last
pair the draw counts differ by less than 0.4%, and the index-stage saving still
holds. Overall FPS changes have mixed signs; NPC activity and texture decode
work vary. All shader compilation counters stay zero. This supports enabling
the bit-exact index optimization by default (CPU-work flags 1283), without
claiming that it alone fixes pacing or delivers a particular FPS increase.
Normal-build measurement remains separate from these instrumented results.

Raw blocks/report are local `topology-copy-profile-blocks.json` and
`topology-copy-profile-report.json`. Captures, raw profiles, generated
translations and executable files stay local.

The subsequent normal-build attempt (`20261012-043632-285`) completed its
timed blocks but reached the runner's timeout before a normal shutdown.
Its presentation CSV was not flushed. That attempt also overlapped another
Haze instance during part of the session. It supplies no valid normal-build
FPS or pacing result; the stage-cost evidence above remains the basis for
the optimization.
