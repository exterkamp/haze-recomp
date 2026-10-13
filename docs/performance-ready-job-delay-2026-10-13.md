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
No SDK change is required. The new candidate has not yet supplied matched
timing evidence; the tables above describe the rejected byte-flag candidate.
