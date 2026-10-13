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
