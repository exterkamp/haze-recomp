# Asynchronous job execution and completion waits

Tracking: #37 (native jobs), #39 (completion), #36 (worker reuse), #32 (frame
pacing). This is diagnostic evidence, not a performance improvement.

## Instrumentation

The title trace now captures the effective route after the mode-3 lookup, the
already loaded slot base, and state publication in `00682100`. State 0 clears
or recycles a slot; state 2 publishes completion. These hooks add no guest
memory reads and preserve the preceding generated operations exactly.

The SDK supplies a startup-registered callback in Windows profile builds. It
copies an immutable job ID, image ID, taskset/task identity and captured R3
arguments before creating the worker. Separate intervals cover CreateThread,
worker setup, the initial lifted ABI call, post-execution work, and thread
cleanup. Failure phases are distinct. Setup includes possible taskset and
serialization waits. Persistent tasks without a return do not produce a
completed ABI interval; title completion publication can precede ABI return.
The SDK's special repeated-resume paths are not fully divided into ABI phases.

The snapshot is copied before CreateThread because the worker may free its
job before CreateThread returns. The callback must remain unchanged after
workers start. Ordinary builds retain only the no-op registration function;
they contain no phase probes or per-job trace snapshot. Scheduling is unchanged.
Every asynchronous job still creates a host thread.

The title recorder now retains 262,144 completed events; recorder plus shutdown
scratch storage remain below 64 MiB. There is no per-event file output or new
interactive performance setting. Older captures remain readable.

## Capture and its limits

Candidate SHA-256:
`6BC93A1CB5D53B7F909A1621473AF393F6274A04AC04CF3057AF968BACB25A23`.

The copied real save resumed in the visually confirmed jungle. The Nectar
tutorial was dismissed before repeated absolute camera sweeps. Settings were
1440p, 8192 shadows, VSync off and uncapped; audio was muted. Own compilation
finished before launch, using two build jobs. The real save was not modified.

The original two 10-second windows cannot support a complete joined analysis:
the first lost its job history; the second lost its first 15.7934 ms of history
and its process observer stopped before the closing boundary. Those original
files and the failed audit are preserved. An earlier eight-second request was
rejected by the recorder before measurement and produced no benchmark window.

A derived interval is explicitly bounded by retained trace history, whole pump
frames, and the last process observation:

- QPC frequency: 10,000,000; interval `[13922887926488, 13922976476565]`.
- Duration: 8.8550077 seconds; trims 28.6486 ms at the start and 1116.7132 ms
  at the end of the original second window.
- Camera, mode, and Haze-process audits pass for this interval. Only the owned
  Haze process was observed. Sampled host memory was about 55%.
- The separate compiler-pressure observer ended before measurement. Two other
  compiler processes existed at its last sample. Compiler activity and other
  application contention during measurement are **not excluded**.
- 262,144 retained events; 8,866,729 earlier tickets overwritten. No busy,
  stale, frozen, missing-event or unfinished-scope counts. The derived interval
  is fully retained; that does not make the discarded interval complete.

The derived present report contains 558 full present intervals: **63.12 FPS**,
mean **15.842 ms**, p95 **19.433 ms**, p99 **28.602 ms**, maximum **49.243 ms**.
Six intervals exceed 33.333 ms. This is a short profiling-build observation,
not an ordinary-build baseline or matched before/after gain.

The pump trace contains **559** fully contained pump frames over the same QPC
bounds. All job costs below use that denominator. Present and pump interval
boundaries differ; their frame counts must not be silently interchanged.

## Joined wait phases

All retained title submissions use effective route 2, the SPURS queue. The
512 slot bases have stride `0x1450`. SDK R3[0] matches a retained title slot
base for 26,571 of 29,178 SDK submit points.

`analyze_job_trace.py` joins each matching SDK submit to the latest preceding
title submit at that exact base. Attribution additionally requires its native
slot generation, exactly one SDK job and one state-2 publication, the same
publication slot base, compatible timestamps, and no reuse before the wait
returns. Missing or ambiguous evidence is counted and excluded. State-0
recycling is not completion. The SDK does not carry the title generation: this
is an observed address/time join, not independent proof of causality.

All 6,076 fully contained waits passed those checks, with no skipped waits or
overlapping attributed waits on the host thread. Costs are elapsed overlap
per pump frame, not CPU cycles or guaranteed removable time:

| Caller / title key / SDK image | Wait | Before SDK submit | Worker startup | Setup | ABI execution | After ABI |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `001F6930` / 10 / 41 | 2.411 ms | 0.611 | 0.237 | 0.034 | 1.163 | 0.365 |
| `007E5414` / 0 / 30 | 2.012 ms | 0.126 | 0.124 | 0.019 | 1.540 | 0.203 |
| `00697570` / 18 / 43 | 2.011 ms | 0.736 | 0.363 | 0.033 | 0.732 | 0.146 |
| Other two callers | 0.048 ms | 0 | 0.038 | <0.001 | 0.003 | 0.007 |
| Total | **6.482 ms** | **1.474** | **0.762** | **0.086** | **3.437** | **0.721** |

An additional 0.0018 ms/frame is unassigned between measured phase boundaries.
Before-SDK time can include title queueing and scheduling. Worker-startup time
is elapsed time until the worker begins, not just CreateThread CPU cost. ABI
time can itself include memory helpers and waits. After-ABI time includes
notification, completion queue draining and main-thread scheduling. None of
these categories independently establishes an optimization's attainable gain.

Worker reuse addresses a measured part of the waits, but does not account for
all 6.482 ms. Native whole-job work remains a separate target: job 0/image 30
has 1.540 ms/frame of ABI execution overlapping these waits; job 10/image 41
has 1.163 ms/frame. Parallel workers' aggregate elapsed times must not be added
to the main-thread frame budget.

## Concrete next kernel identity

Runtime image 30 is registered as `spu_0029_at_00EC9A80`, entry
`spu_func_00003080`, full fingerprint `DB819079AF1D1346`, text fingerprint
`0C500CA64A044D04`. Taskset TI_ARGS are captured as four BE32 words and passed
unchanged through the R3 override. The lifted entry derives an EA at R3[0] +
`0x1440` and issues a one-byte GET there. That is inside the title's `0x1450`
slot, outside its payload copy at `+0x10` of size `0x1410`. This proves
slot-relative addressing; it does not establish the whole payload contract.

The statically identified PPU fallback transform remains inactive in this
capture. Before replacing the live job, audit its complete input/output and
completion contract, capture private comparison fixtures, and compare a native
implementation against the existing path. No decompiled game code or fixtures
are committed here.

## Validation and reproduction

- Exact-operation patch preservation, changed-boundary and repeated-patch
  rejection: two tests passed against the private lift.
- Six analysis tests pass: clipping/union/parallel intervals, worker completion
  before CreateThread returns, immutable snapshots/phase order, reused slots,
  publication before ABI return, missing/ambiguous jobs, and wrong publication
  addresses.
- Native recorder concurrency, wrap, generation/nesting, route/base inheritance,
  explicit SDK intervals, shutdown freeze, and ordinary no-op tests pass.
- Profile build and confirmed live checkpoint pass. The full ordinary build
  suite was not rerun for this diagnostic-only change.

Private evidence prefix: `local/jungle-job-async`; run
`20261013-082459-105`. The original report failure and windows are preserved.
Derived files use `-retained-windows.json`, `-retained-report.json`, and
`-retained-analysis.json`. Build/test logs use `local/job-async-trace-*.log`.

```powershell
.venv/Scripts/python.exe scripts/analyze_job_trace.py local/jungle-job-async-trace.csv --windows local/jungle-job-async-retained-windows.json --pump local/jungle-job-async-pump.csv --output local/jungle-job-async-retained-analysis.json
```
