# Native texture mutation tracking feasibility

The light carrier profile reads approximately 110–112 MB of texture sources
each frame, costing 3.25–3.33 ms in full-content hashing. This motivates testing
native Windows write tracking before changing the game allocator or cache.
No Haze allocation or rendering behavior changes in this commit.

`tests/benchmark_write_watch.cpp` reserves two 4-GB arenas, one with native
write tracking and one without. Both commit a 256-MB prefix and initialize
96 separate 1-MB synthetic textures. The current SDK full-content hash serves
as the reference and the baseline cost. No game data is used.

Microsoft documents [`MEM_WRITE_WATCH` reservation](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualalloc)
and [`GetWriteWatch`](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-getwritewatch).
Querying with `WRITE_WATCH_FLAG_RESET` retrieves and resets tracked writes in
one call. A separate query/reset sequence can miss intervening producer writes.

## Two approaches

The whole-arena approach queries/resets the complete reservation at frame start
and records every dirty page's generation. Before a texture binding it queries
that texture's pages without resetting, covering writes later in the frame.
The recorded generations preserve writes not bound before a frame boundary.

The regional approach queries/resets only a bound texture's pages and records
each returned page with a new monotonic generation. The texture's stamp is the
maximum generation across its entire page range. A changed shared page then
invalidates every overlapping texture, even after another texture resets its
tracking. Writes after a query remain tracked for subsequent queries. Bounds,
API failure and generation overflow fail closed to the full-content path.
All generation/query operations have one reader; producer writes need no
engine-specific notification wrapper.

Each approach passes a differential mutation oracle: 4,000 randomized
write/snapshot/binding operations, unaligned spans, one-byte tails, overlapping
textures, writes after a snapshot, writes between the last binding and next
snapshot, invalid ranges, a newly committed region at the end of the 4-GB
reservation, and disabled tracking. An independent producer thread performs
128 additional coordinated shared-page writes for each approach. Every claimed
unchanged binding is compared with a fresh full-content hash. These checks do
not prove arbitrary concurrent snapshot consistency or integration with Haze.

## Measured costs

Four rounds alternate the whole-arena/baseline order; the regional probe follows
each pair. Each sample contains eight frames and measures reset, writes and
checks separately. Texture writes scatter first-byte changes across a
permutation of the texture pages. Optional background writes touch a disjoint
16-MB region each frame. Timings below are medians of total milliseconds/frame,
including writes; clean-query timings alone would hide the write-fault cost.

| Texture pages written/frame | Background MB | Full hash, untracked | Whole-arena tracking | Regional tracking |
| ---: | ---: | ---: | ---: | ---: |
| 0% | 0 | 3.126 | 1.702 | 0.128 |
| 1% | 0 | 2.950 | 3.892 | 2.298 |
| 10% | 0 | 3.029 | 6.176 | 4.445 |
| 100% | 0 | 3.139 | 16.289 | 14.453 |
| 0% | 16 | 3.172 | 3.653 | 0.171 |
| 1% | 16 | 3.152 | 5.905 | 2.403 |
| 10% | 16 | 3.095 | 7.964 | 4.528 |
| 100% | 16 | 3.242 | 18.333 | 14.515 |

Thread CPU-cycle accounting supports the same direction: without background
writes, 0%-dirty baseline/regional medians are 14.564M/0.599M cycles per frame;
at 10% dirty they are 14.027M/20.802M. Cycles exclude descheduled time but remain
subject to frequency/core placement. The short synthetic tests do not establish
gameplay FPS gains. Regional results always follow the corresponding pair;
cache warming and fixed synthetic layouts limit direct generalization.

Whole-arena reset takes about 1.7 ms even in the clean case and introduces
write overhead for unrelated producer data. Do not integrate this version.
Regional tracking removes most checks for clean sources and avoids resetting
unrelated pages. However, scattered writes can dirty most texture descriptors
even when only 1% of source pages change; both tracking variants are slower
than the baseline at 10–100% dirty. Haze's actual texture-page mutation rate and
writer overhead must be measured before enabling regional tracking.

The next experiment should register an explicitly opted-in tracked reservation,
measure regional queries/dirty pages/skipped bytes and producer CPU cost in
the real carrier, and validate proposed skips against full hashes. On a
validation mismatch without an intervening write, disable the optimization.
Creation, eviction, hash-algorithm switches and failed texture refreshes must
invalidate the stored page stamp. Ordinary-build paired comparisons must
include the allocation and writer costs, not just reduced renderer hash time.

Reproduce:

```powershell
./scripts/benchmark_write_watch.ps1 |
    Set-Content -Encoding UTF8 local/native-write-watch-probe.csv
```

Output is CSV on stdout; mutation-oracle results use stderr. `tracked=0/1/2`
means untracked full hash / whole-arena tracking / regional tracking.
Final artifacts: `local/native-write-watch-final-probe.csv` and
`local/native-write-watch-final-summary.json`. No binary or generated/game data
belongs in the commit.
