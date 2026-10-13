# Idle-worker phase capture: remaining dedicated-thread startup stalls

## Scope and validity

Diagnostic run `20261013-103106-803`, owned PID 47144, used a fresh disposable
copy of the real jungle checkpoint. The tutorial was dismissed and Nectar
activated before two 24-second absolute camera sweeps, with five-second
warmups: reuse **0 then 1**, tokens 2120–2121. Resolution 2560x1440, shadows
8192, VSync off, frame cap zero; native job-0 arithmetic and prior renderer
optimizations fixed. Audio was muted; normal close flushed the recorder.
All seven immutable baseline-save hashes remained unchanged.

The profile binary SHA-256 is
`6A4B38139F4D79E294C9A5A44913F451F89B0277FA82FA23555F2E8A22F86E2B`.
It is b1ca506 / SDK 4bef1a5 plus the larger diagnostic ring. This is not the
ordinary WIP: instrumentation overhead affects timings.

Mode, camera, present continuity and Haze-process audits pass. Eleven pressure
samples bracket the windows, with maximum gap 5.344 seconds, memory
47.78–48.59%, and only the owned Haze observed. **Compiler isolation fails**:
two compiler processes are first observed at QPC 13997032403785, inside the
second window. A subsequent process inspection identifies an independent
`.worktrees/menu-quit` build requesting eight jobs. No compiler was launched
by this test during measurement. Sampling does not prove exactly when the
external compilation began or whether it caused any particular stall.

The first window is QPC 13996553853417–13996793859590; the second is
13996843877510–13997083881414, frequency 10 MHz. Preserve both complete windows:

| Reuse | Present intervals | Mean ms | FPS | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|---:|
| 0 | 1620 | 14.814 | 67.505 | 19.029 | 24.846 | 48.167 |
| 1 | 1708 | 14.040 | 71.223 | 18.253 | 28.018 | 74.037 |

These are descriptive diagnostic results, **not an isolated ordinary-build
gain**. The earlier 3.1-second stall did not recur in these windows; its cause
remains unresolved. The separate ordinary reversed-order confirmation still
contradicts an unconditional gain; see the companion idle-workers report.

## Complete history and wait attribution

The ring expanded from 262,144 to 2,097,152 events. Current slot plus scratch
storage is 400 MiB, bounded by a 512 MiB static assertion, compiled out of
ordinary builds. Actual retention was verified, not inferred from capacity:

- Retained tickets 2443465–4540616, exactly 2,097,152 consecutive events.
- Missing tickets, busy/stale/frozen drops, incomplete scopes and missing
  parents: all zero.
- Maximum discarded end QPC 13996269064395 precedes both window starts.
- 17,854 and 18,357 overlapping title completion waits were matched and
  partitioned; no skipped waits in either window. Partition conservation and
  mapping to complete pump frames pass.

The following are wall-time overlaps of validated title waits with complete
pump intervals. They are not additive with renderer timings or removable CPU
cost. Fastest half and slowest 1% are selected separately within each window;
their workloads and ordering can differ.

| Reuse / group | Frames | Pump mean ms | Wait-union ms | Startup ms | ABI execution ms | Uncovered interval ms |
|---|---:|---:|---:|---:|---:|---:|
| 0 / fastest half | 810 | 13.083 | 4.843 | 0.433 | 2.348 | 8.240 |
| 1 / fastest half | 854 | 12.205 | 3.762 | 0.025 | 1.859 | 8.442 |
| 0 / slowest 1% | 17 | 33.239 | 17.110 | 8.665 | 3.807 | 16.129 |
| 1 / slowest 1% | 18 | 37.476 | 13.894 | 4.744 | 2.577 | 23.582 |

Per-dispatch creation/reuse-call start to setup-start wall time supports the
intended mechanism for the three enabled images. It includes scheduling;
parallel jobs overlap and these averages must not be summed into frame cost.

| Image | Dedicated mean / max ms | Reuse-window mean / max ms | Dispatch policy in reuse window |
|---|---:|---:|---|
| 30 | 0.61743 / 17.1494 | 0.02160 / 7.3154 | Reuse allowed |
| 41 | 0.33912 / 18.4147 | 0.01835 / 4.9867 | Reuse allowed |
| 43 | 0.36219 / 18.6906 | 0.01176 / 0.9482 | Reuse allowed |
| 32 | 0.25048 / 15.0274 | 0.33818 / 20.3848 | Dedicated |
| 33 | 0.79251 / 17.5437 | 0.57206 / 24.0068 | Dedicated |
| 40 | 0.92370 / 17.1370 | 0.60200 / 20.3803 | Dedicated |
| 42 | 0.16275 / 9.8719 | 0.24323 / 15.8902 | Dedicated |

In the reuse window, pump frame 36363 takes 60.376 ms, with 18.075 ms of
matched startup overlap. Four image-33 jobs around QPC 1399698555xxxx each
take roughly 21.5–24.0 ms between SDK submission and setup; all use dedicated
thread creation (kind 8). An image-40 dispatch in the same interval takes
20.381 ms. Frame 36216 takes 44.217 ms, with 14.200 ms startup overlap; an
image-32 dispatch takes 20.385 ms and an image-26 dispatch takes 23.896 ms.
These frame intervals precede the first observed compiler sample, but sampling
cannot establish their isolation. Physics/controller images remain excluded
pending their own lifetime/TLS audits.

The largest reuse pump frame, 36777, takes 74.017 ms: idle wait 59.505 ms,
FIFO scope 6.744 ms, present 4.210 ms, other 3.558 ms. Matched title waits cover
24.655 ms, including 13.543 ms startup, leaving 49.362 ms uncovered by those
waits. This is not proof that reuse or CPU computation caused the whole stall.

Renderer rows provide a separate check: average draw scope is 2.742/2.772 ms,
flush 1.671/1.684 ms, fence wait 1.550/1.562 ms, and GPU timestamp scope
1.375/1.382 ms (off/on). Fence waits are inside flush, and renderer work can
be inside the pump FIFO scope; these numbers cannot be added as separate frame
costs. Texture decode averages 0.025/0.027 ms; tracked resource creation and
shader/PSO compilation are zero in these windows. They do not explain the
observed slow frames here, but this does not exclude them from other captures.

The worst dedicated-mode pump frame, 33662, spends 35.980 ms in FIFO; its
corresponding renderer interval (renderer frame 33665) reports 34.349 ms in
the draw scope. This remains a distinct rendering-CPU-path lead. By contrast,
the worst reuse renderer interval reports 4.035 ms draw scope and 3.890 ms
flush, including 3.713 ms fence wait. It cannot explain the entire 74 ms gap.

## Next implementation boundary and validation

Audit the still-dedicated short-lived images 32/33 first, then 40/42, for
functional TLS, fresh-context initialization, cleanup, dependencies and exact
fingerprints before expanding reuse eligibility. Existing automatic selection
should remain one user-facing behavior, with internal controls for comparison.
Do not infer eligibility merely from a high startup duration. Continue to
investigate the renderer-side multi-second stall independently.

Validation: expanded diagnostic build passed with two compiler jobs; recorder
rollover, four-producer concurrency, nesting, active-scope dump and ordinary
no-op tests passed. Seven trace-analysis tests and four wait-frame tests passed.
The analyzer's stale hardcoded claim that every capture predates 37ffeb7 was
removed. No game-code or SDK behavior changed in this diagnostic commit.

Private evidence prefix `local/jungle-idle-worker-phases`: `-trace.csv`,
`-windows.json`, `-report.json`, annotated `-pooled.json`, `-pump.csv`,
`-renderer.csv`, `-presents.csv`, `-camera.txt.csv`, `-processes.jsonl`,
`-pressure.jsonl`, `-wait-frames.json`, `-async-images.json`. Scripts:
`scripts/analyze_job_wait_frames.py`, `scripts/analyze_job_trace.py`; private
per-image extraction helper `local/analyze-idle-worker-phases.py` uses their
validated joins. Build/tests: `local/idle-worker-phase-build.log`,
`local/idle-worker-phase-trace-tests.log`.

Tracking: #36 worker reuse, #32 native-replacement roadmap, #9 frame pacing.
All remain open; consistent pacing and stable 144 FPS are not established.
