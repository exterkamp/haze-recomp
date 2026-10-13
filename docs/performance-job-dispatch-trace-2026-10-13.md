# Audited job dispatch and transform timing

Tracking: #37 (whole-job native kernel), #39 (submission and completion), #32
(frame pacing). This change adds diagnostics; it does not accelerate the game.

## Measurement boundaries

The build-local patch follows the existing pacing, completion-wait, and VP
patches. SHA-256 guards reject changed function bodies. Removing the inserted
hooks reconstructs the preceding generated source exactly. No generated game
source or decompilation is committed.

The `HAZE_JOB_TRACE` compile gate is enabled only in renderer-profile builds.
`HAZE_JOB_TRACE_CSV` must also name an output file. Ordinary builds compile out
the hooks and recorder; there is no new interactive performance setting.

- Submit `00682648`: entry registers 3–10, caller, TOC and thread identity.
  Both successful slot-allocation paths converge at `00682740`; that boundary
  assigns a native generation to the valid slot before its register is reused.
- Dispatch: record the already resolved CTR/code, TOC, slot base, effective
  mode and descriptor register immediately before the indirect call. Its scope
  includes `DRAIN_TRAMPOLINE`, and therefore any synchronous completion work.
- Handler `00682900` and transform kernel `007E56E0`: separate entry/exit scopes.
  The handler's tail trampoline runs after its scope returns.
- Completion wait `00682538`: entry slot and observed native generation, with
  the existing predicate, queue draining and sleep behavior preserved.

All arguments come from registers already available at each boundary; tracing
adds no guest memory reads. Submit, dispatch, handler and kernel are nested
inclusive wall times, not additive CPU costs. Slot generations and parent span
IDs help correlate observations; they do not establish cross-thread causality.
An asynchronously executed handler cannot inherit a submitting thread's TLS
span automatically and remains uncorrelated unless another observed scope
establishes that relationship.

## Bounded recorder and validation

The recorder retains up to 131,072 completed spans, with no event allocation,
file I/O or mutex. Ring and shutdown scratch storage have a compile-time limit
below 64 MiB. Sequentially consistent writer admission allows shutdown to
freeze publication and wait for current writers without waiting for unfinished
game calls. Per-slot guards reject collisions; sequence checks prevent a
delayed writer from replacing a newer wrapped record.

The CSV reports drops, overwritten ticket history, maximum discarded end-QPC,
unfinished scopes, absent parent records and QPC frequency. The analyzer treats
incomplete windows as lower bounds, clips intervals to measurement windows and
unions each kind's spans per host thread. Different groups must not be added.

Validation completed:

- Two patch tests: exact operation preservation/composition, changed-boundary
  rejection and repeated-patch rejection against the private lift.
- Two analysis tests: nested/disjoint/touching intervals, window clipping,
  simultaneous threads and incomplete-capture reporting.
- Native recorder test: disabled-path invalid-pointer no-op, ring wrap, four
  concurrent producers, generations, parent/depth correlation, invalid slots,
  and dumping while producers and parent scopes remain active. The dump stays
  byte-identical after those producers finish.
- Ordinary-build no-op compile/run, and actual Ninja-rule compile-gate checks.
- Diagnostic build with `-SpuFma -ProfileRenderer -Jobs 2`.

Candidate SHA-256:
`AED3521F97C533C4613B784066EF2179ABF0BBD5A1ACB39DB7DF8BABE5479C19`.
Private logs: `local/job-dispatch-trace-build.log`,
`local/job-trace-tests.log`. A live checkpoint capture follows; no native-kernel
benefit or FPS gain is established by the implementation tests.

## Confirmed jungle capture

Run `20261013-075521-183`, owned PID 2820, uses the private copied save and
visually confirmed jungle checkpoint with the Nectar tutorial dismissed.
The frozen graphics snapshot and actual boot log agree: 2560x1440, FXAA, AF16,
8192 shadows, VSync off, cap zero. Audio device output is muted while mixing
remains active. Native texture reuse, GPU occurrence mode 2, CPU flags 13571
and native VP consumer remain enabled; native producer and native wait remain
disabled. No builds or screenshots occur inside the timed windows.

Both 24-second absolute camera sweeps pass mode, actual-angle and sampled Haze
process-isolation audits. Other agents' instances present earlier in setup had
closed by measurement. The pressure observer samples 44.21–44.34% physical
memory use and zero compilers during the windows. This excludes the earlier
high-memory compiler condition in these samples, not all other host activity.

| Camera token | Frames | FPS | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|---:|
| 1970 | 1491 | 62.17 | 16.085 | 20.667 | 32.353 | 63.131 |
| 1971 | 1531 | 63.83 | 15.666 | 20.390 | 33.302 | 60.542 |

These are diagnostic-build observations, not a matched ordinary-build gain.
The second window's disjoint pump means are 9.492 ms waiting for commands,
4.186 ms processing FIFO, 1.882 ms presenting, and 0.106 ms tick/other. The
producer can be executing or waiting while the pump waits; these are not
automatically removable delays.

The recorder retains 131,072 completed spans out of 720,688 issued tickets,
with zero busy/stale/frozen drops, missing retained tickets or incomplete
scopes. Its discarded-end cutoff falls inside the first window, making that
window's job totals partial. **The entire second window is retained.**

In the fully retained window:

- 55,720 submit spans consume 0.338 ms/frame of unioned elapsed time.
- No synchronous dispatch, `00682900` handler or `007E56E0` kernel spans occur.
  Thus the Ghidra-identified PPU kernel is not executing in this window. The
  writable table's static default did not identify the live work path. This
  does not rule out related work in an asynchronous/SPU implementation.
- All 16,863 observed wait spans match a retained submit slot/generation.
  Their per-thread interval union is 6.381 ms/frame, entirely on guest thread
  1 / host TID 10300. This is elapsed completion-loop time, not measured worker
  CPU cost and not an additional component to add to the pump partition.

| Wait caller | Spans | Unioned elapsed ms/frame |
|---|---:|---:|
| `001F6930` | 12,264 | 2.257 |
| `00697570` | 1,583 | 2.052 |
| `007E5414` | 2,494 | 2.050 |
| `0070BD50` | 21 | 0.018 |
| `0070BCBC` | 501 | 0.004 |

The three dominant callers remain the priority. The next trace must record
the effective asynchronous route, worker execution and completion publication
for these generations. Replacing the inactive synchronous transform function
would not address this capture. No wait is bypassed based on these timings.

Reproduction after normal close:

```text
python scripts/analyze_job_trace.py local/jungle-job-dispatch-trace.csv --windows local/jungle-job-dispatch-windows.json --pump local/jungle-job-dispatch-pump.csv --output local/jungle-job-dispatch-analysis.json
```

Private evidence also includes `local/jungle-job-dispatch-report.json`,
`local/jungle-job-dispatch-pump-summary.json`, the process/pressure logs,
graphics snapshot/hash, and run boot logs. Normal close and profiler flushing
complete successfully; only the owned PID was closed.
