# Readiness-aware job delay experiment

Tracking #39 under #32 and #9. The release policy remains unchanged until
ordinary-build comparisons establish a benefit and gameplay checks pass.

## Producer evidence with diagnostic input detached

Run `20261013-145718-640`, PID 34844, used the ordinary `pad-detach-ordinary`
candidate (SHA256 `65AA01AC0D7FC549E9AFF4A018D4B26B47F4EBDF6B2F405A119147AB35652E6B`),
muted with a disposable copy of the jungle save, 1440p/8192 shadows, uncapped
and VSync off. The tutorial was visually confirmed cleared. PAD_FILE detach
was acknowledged before observers and the five-second camera warmup. Camera
token 2720 controlled the same absolute 30-degree sweep.

The producer, host TID 53436, was sampled only after 20 ms without observed
frame progress, at most three times per unchanged counter. Ninety seconds
yielded 206 samples across 121 episodes, no overwrite. There were no sampled
controller-file opens or camera-control-file opens. Recorded paths include
49 samples in `sys_timer_usleep`, 33 in `sys_cond_wait`, and seven in the
FIFO publication wait. Counts include waiting states and are not CPU cost.

This run is unsuitable for FPS comparisons: another Haze process, PID 50496,
overlapped capture, and measured thread suspensions total 651.867 ms with a
39.297 ms maximum. The maximum counter-poll gap is 105.9934 ms. Resource
samples show 48.18-49.08% memory and no compilers, but do not exclude other
activity between observations. The game closes normally. The unchanged
timing reference remains the validated ordinary detached baseline:
11.711 ms / 85.39 FPS, p99 17.526 ms, worst 46.922 ms.

Several captured waits pass through `func_0071EFE8 -> func_0068EFC0 ->
haze_job_wait_call` inside the title's completion loop. Others belong to the
GCM control path (`func_00734208`, including a 30-us polling delay) or a
predicate-protected condition-variable wrapper (`func_00911198` called by
`func_00896AB0`). These are distinct dependencies. No generic sleep or
condition-wait bypass is justified by these samples.

The existing job trace's state-2 publication marker is emitted by the
consumer while draining completion results. A short interval from that marker
to wait return therefore cannot prove the worker became ready only then.
Worker publication and consumer acknowledgement must not be conflated. The
follow-up below corrects the initial interpretation of the secondary byte.

## Rejected secondary-byte probe

The previous experimental policy removed each 330-us delay and immediately
retried the full completion predicate and pending-work service. Its older
carrier comparisons were mixed and showed roughly two orders of magnitude
more predicate checks; it was never promoted.

The first new experiment retained a 330-us polling deadline inside each guarded
delay, but returns early when the slot's readiness bytes permit the original
completion loop to make progress. It does not re-enter the whole guest loop
on every poll, write completion state, skip the true predicate, or remove
pending-work servicing. OS scheduling can still make a bounded requested
delay finish late; this is not a wall-clock deadline guarantee.

Its audited slot lookup is `func_00682068`: load the anchor from
`TOC - 0x7D6C`, the table from `anchor - 0x8000`, and select
`table + slot * 0x1450`. The initial probe accepted state byte +8 equal to 2,
or state +8 equal to 1 with byte +9 equal to 2. This observation only ended the delay;
the original `func_0071F6F0` predicate still verifies completion and drains
the result queue as needed. The patch guarded both lookup and predicate source
hashes in addition to the existing delay boundaries.

Runtime guards retain guest thread 1, TOC `0xE94EE0`, caller LR `0x00682574`,
slot below 512, syscall 141, sleep LR `0x0068EFF0`, and exactly 330 us.
Resolution validates native RAM spans and rejects invalid/overflowing pointer
chains. It is performed once per delay, with readiness re-read on each poll.
Scoped slot/TOC state restores on nested calls and exceptions. Unsupported
calls retain the original syscall behavior.

The existing internal `HAZE_NATIVE_JOB_WAIT=1` selects this experiment at
startup; default remains disabled. Its former live control-file override is
removed to keep file polling out of this measured path. This is not another
user graphics option. A single native mode event identifies the selected
startup policy; atomic aggregate counts are dumped at normal close, outside
timing windows. Counts distinguish ready returns, expired deadlines and
fallbacks, without per-poll output.

The readiness bytes use volatile byte loads from validated native RAM, with
an acquire fence on the ready outcome. The runtime's native-memory eligibility
gate disables this path under memory probes or barrier diagnostics. The original
completion predicate still consumes the result after the early return.

### Initial candidate validation

The actual guarded boundary and deterministic policy fixture pass under
clang-cl. Coverage includes ready-at-entry, mid-policy readiness, deadline
expiry, invalid range/anchor, diagnostics fallback, disabled-path zero reads,
caller/duration mismatches and exception unwinding. The two Python patch tests
pass, including rejection of changes to each audited function and exact
restoration of the unpatched source apart from the two intended boundaries
and existing profile scopes. These are contract checks, not gameplay evidence.

The ordinary RelWithDebInfo candidate was built with SPU FMA and at most two
compiler jobs. Renderer profiling is disabled. After retaining compatibility
with the benchmark mode-event parser, the final executable SHA256 is
`6010DAE4D79ECD2F29E3EF2FD2340698F720747A6184B74D916B6F4EDD5661D5`.
Both enabled and disabled comparisons used this same executable.

### Matched ordinary comparison

Both runs use the copied jungle checkpoint after visually clearing the tutorial,
1440p/8192 shadows, VSync off, uncapped, CPU mode 13571, worker reuse 1,
VP mode 1 and GPU occurrence mode 2. Each has two 60-second camera windows
with a five-second warmup and acknowledged PAD_FILE detach before timing.
Camera, CPU-mode, native job-wait-mode and Haze-process coverage checks pass.
The other menu-acceptance game observed during preparation had exited before
the measured windows; neither run observed another Haze or a compiler within
its resource coverage. Both games closed normally, with one final counter
report and no periodic FPS lines.

| Policy | Run | Frames | Mean ms | FPS | p95 ms | p99 ms | Max ms | >33.333 ms / >50 ms |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Secondary-byte probe | `20261013-152649-457` | 9,215 | 13.019 | 76.81 | 16.705 | 19.619 | 53.074 | 4 / 2 |
| Original delay | `20261013-153330-720` | 8,942 | 13.417 | 74.53 | 17.102 | 20.288 | 39.322 | 3 / 0 |

Whole-process CPU averages are 4.048/3.970 core equivalents for the enabled
windows and 3.995/4.009 for the original delay. Memory is 47.50-48.01% and
47.58-48.02%, respectively. The resource observation maximum gaps are
5.368 and 5.377 seconds. This single enabled-then-disabled order gives a
roughly 3% mean-interval reduction with worse maxima; it is not a repeatable
pacing win. The older 11.711-ms baseline is a separate run, not the matched
control, and the changed absolute baseline is not explained by these data.

The enabled pump windows spend 6.469/6.562 ms waiting, 4.418/4.457 ms in FIFO
processing and 1.950/1.952 ms presenting. Original windows spend
7.390/7.379, 4.033/4.014 and 1.906/1.900 ms respectively. Less outer wait
does not translate one-for-one into an end-to-end improvement.

The initial native counter reports 126,198,692 early returns and zero expired
deadlines. A separate diagnostic candidate, SHA256
`5858F8C42184A678386B59B5DDCE5DDA06CFFD04B6600CF7D72B110D61F787EF`,
adds a reason split without changing the predicate. Run
`20261013-154051-697` reaches the same visually confirmed cleared tutorial,
detaches diagnostic input and closes normally. All 39,960,154 early returns
are secondary-byte returns; primary returns, expirations and fallbacks are
zero. These are whole-launch counters, not per-window timings, and this
diagnostic run makes no FPS claim.

### Corrected completion contract

Source inspection explains the count: submission copies byte +9 from a global
per-index table via `func_006822B8`; it is not the completion publication
observed by the waiter. `func_0071F6F0` uses that value to decide whether to
run queue service and reread primary state. The initial probe could therefore
return immediately on every retry, recreating the old busy-poll behavior.
It did not skip the original queue service or falsely mark a job complete,
but it was the wrong wake predicate and is rejected.

`func_0071F648` imports `cellSpursQueueSize` (NID `54876603`) and
`cellSpursQueuePopBody` (`91066667`). It drains a completion queue, reads each
record's slot index, and invokes `func_006823C8` to set primary state +8 to 2.
Thus the producer must observe actual queue publication to wake early; the
static secondary flag cannot substitute for it. The native replacement must
leave popping records and marking slots complete to this existing service.

The revised implementation checks a validated nonempty completion queue under
the same SPU lock used by the SDK queue operations. It retains the 330-us QPC
deadline and does not use the existing 1-ms WaitOnAddress timeout. Queue
observation must preserve busy/index-wrap semantics and perform no guest
writes. It remains experimental and off by default pending ordinary comparisons
and gameplay validation. Stable 144 FPS, initial-load
pacing, squad conversation, transitions and long-run acceptance remain open.

## Current queue-based candidate

The guarded delay lazily resolves the queue once per delay when primary state
is 1 and the routing byte is 2. The audited chain is:
`root = read32(TOC - 0x7C1C)`, `table = read32(root - 0x7FD8)`,
`queue = read32(table)`. Each pointer-read span is checked. The queue must be
128-byte aligned with a valid 128-byte header. Under `spu_lockline_lock`, each
probe validates entry size 16, pop direction 1, depth, aligned buffer RAM span,
head/tail ranges and occupancy. One's-complement busy indices stay waiting;
malformed metadata falls back to the original syscall. A valid nonempty queue
ends the delay, leaving all records and indices untouched. Completed primary
state can also end the delay. The unchanged original predicate then drains
the queue and confirms the target slot; another slot's event is only an
opportunity for that original service to make progress.

The QPC deadline still requests at most 330 us of polling, with
`SwitchToThread` / `YieldProcessor` between probes. Lock contention and OS
scheduling can exceed the requested interval. No native event, waiter-list
mutation, dequeue, guest completion write or per-probe log is added. Startup
mode and enabled-only exit counts remain the only new log events. Source
hash guards now cover `006820B0` and `0071F648` as well as the earlier audited
boundaries. Memory diagnostics disable the fast path.

The revised actual-boundary fixture passes with queue wraparound, busy/empty
states, unsupported metadata, invalid pointer/range, diagnostics fallback,
balanced locks, and byte-for-byte unchanged guest RAM. A captured final dump
asserts exactly eight handled calls, three early returns (one primary and two
queue), five expired deadlines and fourteen fallbacks. This ensures empty
and busy cases cannot pass merely by returning any handled result. The two
fail-closed patch tests pass. The fixture uses stub guest RAM and locks; it
does not establish real-thread contention behavior or gameplay correctness.

The ordinary SPU-FMA build, capped at two compiler jobs, succeeds with SHA256
`5E5EE355970F16A0DDA97EDA775B4EFFFDF80432F5D158803B16A2E115F54199`.
No SDK change is required. The following results use this queue-based candidate;
the earlier table describes the rejected byte-flag candidate.

## Queue-policy gameplay measurements

All three runs use the same ordinary binary, copied jungle save, 1440p/8192
shadows, uncapped/VSync-off settings and fixed native modes above. Each has
two 60-second absolute-camera sweeps after visually confirmed tutorial
clearance and acknowledged PAD_FILE detach. Camera, CPU mode, actual job-wait
startup mode, present continuity and Haze-process coverage checks pass. All
close normally, with one final draw-counter report and no periodic FPS output.

| Policy | Run | Frames | Mean ms | FPS | p95 ms | p99 ms | Max ms | >33.333 ms / >50 ms |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Queue wake enabled | `20261013-155159-207` | 11,188 | 10.725 | 93.24 | 13.792 | 16.241 | 81.681 | 2 / 1 |
| Original delay, Print Screen reported | `20261013-155736-621` | 10,309 | 11.637 | 85.93 | 15.383 | 18.590 | 47.469 | 6 / 0 |
| Original delay, repeat | `20261013-160349-416` | 9,495 | 12.635 | 79.15 | 16.124 | 19.214 | 130.291 | 6 / 3 |

These are provisional comparisons, not isolated performance acceptance. No
other Haze instance is observed, but external builds overlap all three runs
(maximum 2, 4 and 2 compiler processes). Memory ranges are 49.25-55.83%,
44.12-46.85% and 45.33-54.56%, respectively. Whole-process CPU core equivalents
for the two windows are 4.059/4.008 enabled, 4.054/3.965 original, and
4.184/4.185 repeated original. Host interference is observed but is not proven
to cause any particular frame dip. The differing controls prevent a precise
causal speedup claim; only one enabled launch is measured here.

The user reported pressing Print Screen during the first original-policy run.
The report was recorded at 16:02:56 UTC; the action's exact timestamp is unknown.
Every sample remains included, and no outlier is attributed to that action.
The complete original-policy run was repeated rather than deleting selected
frames. No intervention was reported for the repeat.

The enabled candidate records 527,290 handled delays over the full launch:
435,426 queue-ready returns, zero primary-state returns, 91,864 deadline
expirations and zero fallbacks. These are whole-launch counts, not counts
restricted to the timing windows. They confirm that the routing flag alone
no longer causes every delay to return immediately. The observed gameplay
and normal close are a smoke check, not scene-transition or long-run validation.

Mean pump phases (wait / FIFO / present / other, ms) are:

- Enabled: 5.389 / 3.399 / 1.844 / 0.092.
- First original run: 5.699 / 4.000 / 1.862 / 0.076.
- Repeated original run: 6.564 / 3.973 / 2.013 / 0.084.

The enabled run's worst pump frame, 24622, takes 81.681 ms: 72.270 ms outer
wait, 5.485 FIFO, 1.978 presentation and 1.948 other, across 48 waits and 53
drains. The repeated control's worst frame, 27716, instead takes 130.292 ms:
128.245 ms in one FIFO drain, 2.032 presentation and 0.0006 outer wait. Its
coarse Windows thread-CPU delta is zero; that alone cannot distinguish driver
wait, lock wait or descheduling. The earlier rare long single-drain stall has
therefore recurred and needs backend/fence and scheduling attribution, alongside
the separate producer-wait stalls. Do not attribute both to the job timer.

Current disposition: promising typical-frame results, with neither stable
144 FPS nor consistent pacing established. Keep the experiment disabled by
default. Repeat enabled/disabled runs under steadier host conditions, capture
the recurring slow-drain path separately, and validate conversation, transitions
and longer gameplay before promotion.

## Follow-up drain diagnostic

An ordinary-build run of the same binary with `GCM_SLOW_DRAIN_CSV` enabled
(`20261013-161821-180`, PID 28276) repeated the copied jungle checkpoint,
original delay, and two 60-second camera sweeps. Camera, mode, present and
process checks pass. It records 10,670 frames: 11.244 ms mean / 88.94 FPS,
p95 14.024 ms, p99 15.983 ms, maximum 592.053 ms. All frames are retained.
This is diagnostic evidence, not a new speedup comparison. Two external
compiler processes overlap; observed memory use is 43.66–46.15%.

The two worst pump frames instead spend most of their time in outer waits:

| Pump frame | Total ms | Outer wait ms | FIFO ms | Present ms | Waits / drains |
| --- | ---: | ---: | ---: | ---: | ---: |
| 65311 | 592.053 | 578.991 | 9.036 | 1.772 | 505 / 542 |
| 65385 | 537.562 | 522.345 | 9.486 | 2.108 | 453 / 487 |

The retained FIFO ring starts before the measured windows. It contains 9,946
slow drains wholly inside those windows, with maximum duration 6.727 ms and
maximum lock acquisition 0.002 ms. Older startup rows were overwritten
(43,375 recorded / 26,991 overwritten); this is not full-launch coverage.
The earlier 128-ms single-drain stall did not recur. This capture therefore
does not resolve that stall, nor establish the cause of the long producer
waits. Raw thread cycles and coarse thread-CPU deltas are not converted into
precise running-versus-blocked time.

Windows Performance Recorder is installed, but a uniquely named smoke trace
failed to start with `0xc5585011` (system-performance profiling policy could
not be enabled). No recording started and no OS policy was changed. The
fallback is ordinary-build in-process phase timing around graphics submission,
fence completion, retirement and reset, joined to FIFO intervals by QPC.

## Ordinary backend phase capture

The SDK now has an optional `RSX_SLOW_FLUSH_CSV` bounded ring, independent of
`YZ_PERF_PROFILE`. It times Close, ExecuteCommandLists, Signal,
GetCompletedValue, SetEventOnCompletion, WaitForSingleObject, info-queue
processing, retired-resource release, allocator Reset and command-list Reset.
Rows include reason, host TID, frame, fence/completed values, retired count,
reached-stage mask, HRESULTs, wait result/error, and unaccounted QPC ticks.
Existing early failures retain their partial phase record. This does not
reorder GPU calls or change the synchronous flush policy. It performs no
hot-path file I/O or GPU timestamp queries. The environment option is for
diagnostics, not a release graphics setting; it is disabled by default.

The ordinary two-job build succeeds with SHA256
`6FD67D99FD3B0AC621DE4B94F76BB6D9DB8B68AFC1E329B30CFC15CD8191649C`.
Eight extracted-source fixture cases pass: disabled QPC/cycle no-op and
unchanged call order, Close/Signal/SetEvent failures, wait/no-wait,
WAIT_FAILED preserving last error before QPC, and reset failures. Nine
analyzer tests pass, covering malformed metadata/partitions, sparse and
overwritten rings, clipped intervals and same-thread versus cross-thread
overlap. These targeted checks and the ordinary build were run; the full
repository test suite was not rerun. Both tests are wired into `build.ps1 -Test`.

Run `20261013-163036-727` (PID 20716) uses the same copied-save scenario and
original delay, with both FIFO and flush rings enabled. Flush threshold is
5,000 us to preserve rare-stall evidence in its 4,096-row ring. Visual
navigation and acknowledged controller detach precede two 60-second camera
windows. Mode, camera, present continuity and Haze-process checks pass;
normal close produces the diagnostic dumps. This is attribution evidence,
not a performance comparison to another binary.

- 10,395 frames: 11.542 ms mean / 86.64 FPS, p95 14.574 ms, p99 17.121 ms,
  maximum 55.570 ms; three frames over 33.333 ms and one over 50 ms.
- External compiler count reaches two; observed memory use 45.20–56.48%.
- FIFO ring: 17,671 recorded, 16,384 retained, 1,287 overwritten. The retained
  event extent surrounds both timing windows. There are 9,732 wholly contained
  slow drains, maximum 7.058 ms; maximum lock acquisition is 0.123 ms.
- Flush ring: two records, no overwrite or failure, both presentation flushes.
  Durations are 6.2045 and 5.2801 ms, with 6.0195 and 5.1933 ms respectively
  inside WaitForSingleObject. Neither overlaps a retained slow FIFO drain.
- Worst measured pump frame 35930 is 55.571 ms: 44.244 outer wait, 6.714 FIFO,
  2.346 presentation, 39 waits and 43 drains. Neither slow flush overlaps it.

The long single-drain stall did not recur. Recorded fence waits therefore do
not explain this capture's worst hitch. Thresholded records do not describe
all flushes, and event extents are not continuous tracing coverage. Keep
producer-wait attribution and rare backend-stall attribution separate.

Analysis command (after normal close):

```powershell
./.venv/Scripts/python.exe scripts/analyze_slow_drains.py local/jungle-flush-attribution-fifo.csv --flush local/jungle-flush-attribution-flush.csv --pump local/jungle-flush-attribution-pump.csv --windows local/jungle-flush-attribution-windows.json --output local/jungle-flush-attribution-attribution.json
```

The analyzer uses QPC intersections, reports clipped events, and keeps raw
thread cycles separate from wall time. Its full-ring worst frames and worst
frames wholly inside the selected windows are separate outputs. No WIP or
native job-delay default is changed by this diagnostic work.
