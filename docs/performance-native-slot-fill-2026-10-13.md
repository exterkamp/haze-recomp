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
