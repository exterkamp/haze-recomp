# SPU context storage: controlled carrier measurements

The 30 FPS gameplay goal remains open. This follows the
[earlier profiling report](performance-2026-10-11.md). All game tests were muted.

## Measured change

Each asynchronous job still gets freshly initialized SPU registers and local
store, executes its compiled entry, preserves DMA/coherency semantics, and
signals its real completion. On Windows, the large context is now allocated
with 16-byte alignment on the heap and released after coherency and MFC cleanup.
Allocation failure falls back to the existing stack path.

The stack wrapper must stay out of line: assembly inspection caught the
compiler folding it into the shared caller, which would have made heap jobs
probe the same large stack anyway. After separating it, the shared job caller's
stack frame is `0x338` bytes. No jobs, simulation steps or visual effects are
skipped to improve FPS.

The isolated first-use benchmark measured about 30,278 cycles for a heap call
and 24,847 for its second call, versus roughly 456,000 and 25,000 for the earlier
stack test. These are diagnostic CPU-cycle measurements, not game frame times.

## Lifetime defect exposed by the experiment

The initial gameplay switch to heap storage crashed in run
`20261011-232400-919`. That run supplies no valid heap performance result.
The reservation-holder registry inserted into an early empty slot before
checking whether the same context already occupied a later slot. Unregister
removed only one occurrence. Thread-stack cleanup masked this defect for stack
objects; freeing a heap object left a dangling duplicate.

The registry now stores a compact, unique live set under its existing lock and
grows when needed instead of silently excluding holders beyond eight. The
public-API regression creates the problematic hole, re-registers and removes
the later holder, protects its memory against access, then sends a notification.
It crashes with `0xC0000005` against the previous implementation and passes on
the fix. Another case verifies reservation-loss delivery to 128 simultaneous
holders and self-exclusion. Fresh-state ABI tests verify full local-store
writes, arguments, mailbox completion and cleanup across storage modes.

## In-game A/B test

Run `20261011-233136-973`, visually checked at the stationary carrier checkpoint
(capture 7964), uses the corrected registry in both modes. No per-job timing,
instruction sampling, builds or readbacks occurred during the blocks. Each
block contains 180 consecutive presents after 80 warmup frames. Flags 3 and 35
keep bulk references and dense remapping enabled; only SPU context storage
changes. Shader cache is warm and `RSX_PROFILE_LIGHT=1`.

| Storage | Frames | FPS | p95 ms | Draw CPU ms | Mean draws | Mean CPU cores* |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| Stack | 8245–8424 | 19.12 | 71.73 | 27.22 | 1441 | 5.91 |
| Heap | 8541–8720 | 21.35 | 62.46 | 24.15 | 1244 | 5.11 |
| Stack | 8841–9020 | 19.64 | 69.56 | 24.79 | 1320 | 6.30 |
| Heap | 9141–9320 | 20.25 | 61.75 | 25.58 | 1385 | 5.36 |
| Stack | 9441–9620 | 18.72 | 72.76 | 27.70 | 1416 | 6.28 |
| Heap | 9741–9920 | 21.36 | 60.96 | 24.72 | 1407 | 5.61 |

*Whole-process CPU counters cover propagation, warmup and the measured block;
their bounds differ from the frame interval. They do not suspend game threads.
NPC activity and draw counts vary, so the entire FPS difference cannot be
attributed precisely to storage. All three pairs favor heap storage in FPS,
p95 and CPU use, including the last pair with similar draw counts. It becomes
the Windows default. `SPU_HEAP_CONTEXT=0` retains the stack mode for comparison.

The corrected run reported no coherency leaks, invalid FIFO calls or command
recovery, and closed normally with exit code 0. Pacing is improved but still
well short of consistent 33.3 ms frames.

## Reproduction

Build with `scripts/build.ps1 -SpuFma -ProfileRenderer -Test`. Set
`RSX_PROFILE_LIGHT=1`, `RSX_PROFILE_CSV=renderer.csv`, and point both
`RSX_CPU_WORK_FILE` and `SPU_EXECUTION_MODE_FILE` at an ignored control file
initially containing 3. Navigate to and visually verify the checkpoint, then:

```powershell
.venv/Scripts/python.exe scripts/benchmark_cpu_work.py <renderer.csv> <control.txt> --flags 3 35 3 35 3 35 --pid <HazePID> --output <blocks.json>
```

The driver restores zero on exit. Remove diagnostic controls for ordinary play.
Use `scripts/benchmark_spu_abi.ps1 -Heap` for the isolated heap measurement.
All raw logs, captures, game assets and generated code stay ignored.

Next, targeted sampling of the recurring long SPU image uses
`SPU_JOB_TIMING=1` and `SPU_JOB_TIMING_IMAGE=40`. Job-start notifications and
thread descriptions let `scripts/profile_spu_image.py` open the current worker
directly rather than relying on system snapshots that miss short-lived threads.
Run that diagnostic separately from FPS benchmarks.

Run `20261011-233652-929` verified the carrier scene in heap-default mode.
The targeted sampler opened 369 of 374 announced image-40 jobs over 20.00 s
and collected 1,990 instruction samples. The largest groups were compiled
functions `0x3450` (572) and `0x35B4` (219), followed by task-launch checking
(186) and `ldexp` (88). Generated code and object-code inspection identify
quadword byte rotates/shifts in those functions as the next optimization
candidate. Sample counts include helpers and waits and are not exact CPU
utilization percentages. The profile run closed normally with exit code 0.
