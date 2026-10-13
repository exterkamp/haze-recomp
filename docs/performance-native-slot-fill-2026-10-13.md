# Audited native job-slot fill

Tracking: #38. This is an experimental replacement, not a measured speedup or
release promotion.

## Selection and operation

Sparse CPU caller samples identified `func_00C11EA8` among callers of the
64-bit memory-write helper. That is a lead, not an estimate of its frame-time
share. Source inspection found hundreds of callers of this generic fill, so
the experiment is restricted to the known job-slot initialization call in
`func_00682648`, return address `0x00682774`.

That path obtains a selected slot, clears `0x1450` bytes with zero, copies a
`0x1410`-byte payload, and then continues descriptor setup and publication.
The source patch verifies normalized-text hashes of both complete functions;
changed generated input fails closed and requires re-audit. The original game
code and extracted test oracle remain private build inputs.

The hook sits after the original pre-loop branch and before the loop label,
so a fallback calls the helper only once, not on every loop iteration.
Eligibility requires the exact caller, zero pattern and slot size, canonical
addresses, and the scratch-register relationships established by the original
alignment head. Only the bounded 64-byte loop body is replaced with `memset`.
The original head and tails remain. The helper restores the loop's exit cursor
and final-block pointer; the original continuation immediately recomputes CR0.

SDK `70a8e56` adds a one-destination span API. It rejects unavailable native
memory, diagnostic modes, zero/oversized spans, non-ordinary address windows,
HLE control memory, bounds violations and any intersecting SPU reservation.
The reservation lock covers both the scan and the native write; the original
accessor path retains all behavior on rejection. The span API does not itself
establish game ownership: the audited call-site restriction supplies that
boundary. It does not make arbitrary shared game memory eligible for bulk
writes.

## Validation and measurement controls

`tests/test_bulk_fill.py` extracts the actual original and patched function
plus the generated rotate helpers, all fingerprint-checked. It compares the
complete PPU context and memory arena, including bytes outside the destination.
Cases cover all eight base alignments, unrelated callers, nonzero fills,
length edges, noncanonical inputs, and reservation/availability fallbacks.
It links the real SDK span implementation; only memory availability and the
lock/reservation primitives are stubbed. Direct API cases cover edge lines,
HLE overlap, low memory, VM bounds, the inclusive 1 MiB cap, and rejection
without a held lock or modified memory.

`HAZE_NATIVE_BULK_FILL=ON` is an exact, immutable developer comparison selector;
default, typo and OFF retain the original loop. Fresh-process tests check these
cases and changing the environment after initialization. This adds no graphics
setting. A one-time success log establishes that at least one fill ran natively,
but cannot prove the path remains useful after loading.

Separate optional `HAZE_BULK_FILL_PROFILE=1` aggregate counters record eligible
attempts, successes, rejections and bytes at exit. Without that selector there
are no aggregate counter operations. With it, the focused fixture verifies
13 attempts, nine successes, four rejections and 46,656 native bytes. Whole-run
counts are not timing-window counts and do not identify rejection causes.
They are needed because persistent reservation bits on reused slots could make
the fast path ineffective after startup. Ordinary FPS comparisons must disable
the counters and all draw/FIFO tracing.

Haze integration `432bb9b` pins SDK `768f733`, which also exposes the separate
slow-draw diagnostic to ordinary builds. The two-job SPU-FMA ordinary build and
full regression suite exited 0, including the new fill and ordinary/profile
draw tests (`local/native-slot-fill-build-tests.log`). The executable is
122,752,512 bytes, SHA-256
`6DF55401360CA7AA2666D6E5EBDC185F29DE2A136FC5BB42573DE5601D375B6B`.
Gameplay measurements remain pending.

## First gameplay diagnostic: native path active, timing contaminated

`jungle-slotfill-diagnostic-a1` used that exact binary with native fill ON,
aggregate counters ON, inline reads and reservation-predicate experiment OFF,
plus FIFO/flush/draw diagnostics. It used the same copied jungle save and
1440p/8192-shadow uncapped settings as the preceding FIFO diagnostic. Setup
was visually confirmed, input-file detach acknowledged, audio muted, and
both camera sweeps and the owned game exited normally with code 0.

Whole-launch counters recorded **419,439 eligible attempts, 419,439 native
successes, zero rejections, and 2,174,371,776 native bytes**. These counts show
the experiment executed extensively rather than merely once at startup.
They do not establish a time saving, count only the already shape-eligible
calls, and are not restricted to the timed windows. The suspected sticky
reservation fallback did not occur in these eligible calls.

Both diagnostic camera/mode checks passed, but the strict process-isolation
gate failed. Other Haze PID 36484 overlapped; resource observations found up
to **12 external compiler processes**, memory usage of 60.68–80.49%, and a
maximum sample gap of 5.453 seconds across 26 bracketing samples. No own
compiler, CPU sampler or screenshot ran in the timing windows.

All 2,935 measured frames are retained: 24.569 FPS / 40.702 ms mean,
170.158 ms p95, 408.739 ms p99, 783.318 ms maximum; 511 frames exceeded
33.333 ms and 402 exceeded 50 ms. The two windows differed sharply:
8.830 FPS / 113.256 ms and 40.404 FPS / 24.750 ms. This is an instrumented,
compiler-contended run, not a usable ordinary-performance comparison.
Its worst pump frame spent 735.722 of 783.319 ms in outer waiting.

The ordinary slow-draw recorder did not export a CSV. The live test exposed
a missed normal-exit path: Haze calls `rsx_live_draw_dump_present_samples`
then exits without renderer teardown, and that function still guarded the
draw dump with `YZ_PERF_PROFILE`. The initial extraction fixture tested the
recorder and callback wrapper but not this actual exit entry. The follow-up
fix and test must cover that entry before repeating the diagnostic. The
missing file is a failed capture, not evidence that no slow draws occurred.

Private evidence: `local/jungle-slotfill-diagnostic-a1-comparison-evidence.json`,
the matching windows, process/resource observations, FIFO/flush/pump/present
CSVs, and `local/jungle-slotfill-diagnostic-a1-slow-analysis.json`.

SDK `01f1bfc` moves the draw export into the common normal-exit path. The
focused actual-source fixture now calls that entry, including repeated export,
in both ordinary and profiling configurations; both pass. The draw analyzer
also supports an explicit ring capacity, preserving its historical 256 default
while validating the new 1,024-record recorder (10 Python tests pass).

Haze integration `a7bd464` pins that SDK. The incremental two-job build exited 0;
the preceding full suite covers unchanged code. Corrected executable SHA-256:
`01EEFF9C814582148B475F45A82C881FFEF8BE72750F161F2D0F329D277CD47A`,
122,752,512 bytes. Its actual gameplay export still needs confirmation.

## Corrected diagnostic export verified

`jungle-slotfill-diagnostic-a2` uses the corrected executable above, with the
same native-fill/counter/trace settings and copied jungle scenario. Preparation
was visually confirmed; input polling detached; the two 60-second camera
windows and owned game completed normally with code 0. The seven baseline
save files remain private and were copied, not used as the writable fixture.

All 9,229 measured frames are retained: **76.933 FPS / 12.998 ms mean**,
16.605 ms p95, 19.415 ms p99, 50.641 ms maximum. Two frames exceeded 33.333 ms
and one exceeded 50 ms. Camera/mode checks passed, strict isolation failed
because another Haze process (PID 60056) overlapped. Twenty-six bracketing
resource samples observed no compiler, memory usage of 50.40–51.53%, and a
maximum 5.376-second gap. This remains an instrumented shared-host diagnostic,
not an enabled/disabled speedup comparison.

Whole-launch fill counts: **671,430 attempts, 671,430 native successes, zero
rejections, 3,480,693,120 native bytes**. As before, these are not isolated to
the timing windows. No native-fill performance gain is inferred from them.

The slow-draw export retained 61 callbacks at or above 4 ms, out of 6,780,019
total callbacks, with zero overwritten records. FIFO and flush rings also had
zero overwrites. The analyzer validated raw QPC intervals, phase conservation,
cycle metadata and recorder counts using explicit capacity 1,024.

Only one retained draw was fully inside the timing windows: 4.5325 ms,
including 4.4252 ms geometry preparation. The largest gameplay pump frames
were instead 50.6410 ms (44.3162 ms outer wait) and 36.3272 ms (29.6382 ms
outer wait). Neither contained a retained draw over the 4 ms threshold.
Subthreshold draws and upstream work remain possible costs; this run did not
reproduce the earlier 148 ms timed FIFO body.

Before the timed windows, the trace did capture expensive draw callbacks:

| Draw callback | Wall ms | Geometry ms | Pipeline/constants ms | Resources ms | Recording ms |
|---|---:|---:|---:|---:|---:|
| 1 | 40.8637 | 0.0276 | 40.7281 | 0.0554 | 0.0526 |
| 1244397 | 30.7068 | 0.0135 | 30.6023 | 0.0203 | 0.0707 |
| 1243264 | 22.1049 | 0.0049 | 15.3331 | 6.7073 | 0.0596 |
| 1272615 | 20.0380 | 0.0021 | 19.0449 | 0.9222 | 0.0688 |

These startup/loading observations point to the pipeline/constants stage as
a concrete lead for initial stalls. That stage includes `get_pso` and constant
preparation/upload; this trace does not distinguish shader generation,
compilation, PSO creation, upload-related waits, or descheduling. More precise
timing is required before attributing those spikes to shader compilation.
Raw thread cycles are retained without conversion into CPU milliseconds.

Next: compare fill enabled/disabled in the same binary with all diagnostic
counters and traces off; investigate the measured pipeline/constants startup
cost separately from producer-side steady-gameplay waits.

Private evidence: `local/jungle-slotfill-diagnostic-a2-{comparison-evidence,draw-analysis,slow-analysis,draw-joins}.json`
and matching source CSVs, window manifest, exit log and resource observers.

## First ordinary enabled/disabled comparison: inconclusive

Both launches use the corrected executable SHA above, the same private-save
baseline, 1440p/8192 shadows, uncapped presentation with VSync off, fixed CPU
flags 13571, GPU occurrence mode 2, native VP consumer and worker reuse on.
Inline reads, inline coherence, native job-wait replacement, bulk-fill counters,
and slow FIFO/flush/draw traces are off. Only native slot fill changes. Each
launch reaches the jungle checkpoint with a visually verified capture, detaches
diagnostic input polling, then records two 60-second absolute camera sweeps.
The control remained at the checkpoint longer before measurement, so initial
settling time is another limitation. Every complete timed interval is retained.

| Launch | Fill | Frames | FPS | Mean ms | p95 ms | p99 ms | Maximum ms |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `jungle-slotfill-off-a1` | OFF | 8,683 | 72.385 | 13.815 | 18.115 | 21.960 | 43.809 |
| `jungle-slotfill-on-b1` | ON | 8,332 | 71.059 | 14.073 | 18.155 | 22.897 | 2,411.222 |

Both owned games and measurement scripts exited normally. Camera and mode
validation passes, but strict process isolation fails in both. The control
observes another Haze PID 60056, no compilers, memory 52.14-54.09%, and a maximum
5.369-second resource sample gap. The enabled run observes PIDs 60056 and 44672,
up to two compilers, and memory 52.04-54.28%. Its maximum resource sample gap is
9.013 seconds, **failing the 8-second coverage requirement**. The initial report
stops at that check; the retained diagnostic report records `coverage_passed:
false` explicitly without discarding frames or claiming validation succeeded.

The enabled run's largest pump interval spends 2,400.241 ms of 2,411.223 ms in
FIFO processing, with 15.625 ms coarse render-thread CPU accounting. Another
2,088.550 ms frame spends 2,046.465 ms in the outer wait; a 420.541 ms frame
spends 414.090 ms in FIFO processing. These elapsed stalls are real observations;
without nested traces this capture cannot identify their cause or assign them
to the overlapping processes. They are not removed from the reported averages.

There is **no demonstrated slot-fill speedup**. Host activity and settling differ,
and the enabled run fails resource coverage. The replacement remains experimental
and disabled by default. Follow-up should separate recurring FIFO stalls from
producer waits and repeat the ordinary comparison under comparable conditions.

Private evidence: `local/jungle-slotfill-{off-a1,on-b1}-comparison-evidence.json`,
matching camera/present/pump CSVs, window manifests, strict/diagnostic reports,
process/resource observations, and normal-exit logs. The original failed report
is retained at `local/jungle-slotfill-on-b1-evidence.log`.
