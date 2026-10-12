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
restart conversion. The default remains 259 until native comparison supports
changing it. A new `topology_index_ms` scope times this work separately.

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

These are synthetic stage costs, not FPS gains. Repeated native off/on blocks
and normal-build measurements are required before claiming gameplay benefit.
Captures, raw profiles, generated translations and executable files stay local.
