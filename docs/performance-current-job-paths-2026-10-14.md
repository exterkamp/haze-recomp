# Current native-job waits in the jungle checkpoint

Related: #37, #39, #71; roadmap #32.

The current worker-reuse build spends much more of its observed main-thread
job waits before SDK submission and during job execution than starting workers.
This identifies the next scheduling investigation; it does not establish a
release speedup or prove that all pre-submission time is idle or removable.

## Build and capture

`8cbb70c63a126a3bb89c28778d4996231aa0341b` adds `-ProfileJobs`, enabling
the existing title job and SDK asynchronous-phase probes independently of
broad renderer profiling. SDK pin is
`7574134df9e0f243b147220917031abcef5059ee`.
Actual Ninja rules pass renderer=0, jobs=1, inline-reads=0 validation.
The build succeeds with two compiler jobs. Configuration tests and job trace
analysis fixtures passed; the median-report change passes all four existing
present-mode tests.

Candidate: `local/candidates/producer-jobs/HazeRecomp.exe`.

- EXE SHA256: `6863FA070DD7615A9C87D23FCF15196A1910BDDFA4A95FAE06F3183D16851F23`
- PDB SHA256: `8B38F1659156382CA9A76E790B81913E012888CCA0553C4E8E5E2805CFAADCE4`
- Run: `20261014-023731-567`, PID 16784, main host TID 47224.
- Evidence prefix: `local/jungle-producer-jobs-a1-*`.
- Private baseline save, config and seeded shader cache; 2560x1440, FXAA,
  AF16, 8192 shadows, VSync off, uncapped. Existing GPU occurrences/native
  indices and worker reuse remain enabled. Experimental early completion,
  slot copying, inline reads/coherence and bulk fill remain off.
- Muted, milestone and boot-report logging off; producer, title job-loop and
  asynchronous job traces enabled. Diagnostic input detached after tutorial
  completion and visual confirmation that the Nectar gauge drained.
- Two 20-second absolute camera sweeps, tokens 3800/3801, QPC frequency 10 MHz:
  `14577035293634..14577235298414` and
  `14577285311581..14577485316808`.

## Frame timing and coverage

| Capture | Frames | FPS | Mean ms | Median ms | p95 ms | p99 ms | Worst ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Current job-instrumented run | 3,588 | 89.763 | 11.140 | 10.893 | 14.028 | 16.042 | 21.112 |
| Previous ordinary copy-off baseline | 8,598 | 95.559 | 10.465 | 10.206 | 13.154 | 15.097 | 22.454 |

The second row is the earlier ordinary result documented in
[slot-copy measurements](performance-slot-copy-recycle-2026-10-14.md).
These are different captures with different instrumentation and non-replayed
NPC state: the difference is not an optimization result or an isolated measure
of tracer overhead. All measured intervals remain included. Neither has an
interval over 33.333 ms. Stable 144 FPS requires 6.944 ms, including the tails.

Current camera/mode/process checks pass. Resource monitoring brackets both
windows: 11 samples, maximum gap 5.368 seconds, physical memory 54.57–54.93%,
zero observed compilers or other Haze instances. No owned build, screenshot or
heavy offline analysis overlaps timing. The owned game exits normally with
code zero; all seven immutable baseline save hashes match.

The job trace retains 2,097,152 consecutive events after 5,602,890 older
tickets. There are zero missing/busy/stale/frozen/incomplete spans or missing
parents. Maximum discarded end QPC `14576929502378` precedes both windows.
Strict wait-to-frame analysis passes with zero skipped wait joins in both.
The producer trace's three unfinished scopes begin after both windows;
its retained-window analysis reports no overlap/clock-loss reasons. Its CSV
lacks a capture-start timestamp, so its generic lifetime-proof flag remains
false, as in previous captures. Job-loop snapshots exclude active loops and
are used for completed-loop phase totals, not a stronger completeness claim.

## Where the matched waits spend time

The following values average fully contained, validated main-thread waits
over both windows, weighted by pump-frame count. They partition the observed
waits, not the whole frame. They overlap other threads' work and cannot be
added to renderer or producer inclusive times.

| Phase | Wall ms per pump frame |
| --- | ---: |
| Before SDK submission | 1.340 |
| SDK submission to worker setup start | 0.023 |
| Worker setup | 0.072 |
| Overlap with worker ABI execution | 1.924 |
| After ABI execution until wait ends | 0.625 |
| Unattributed | 0.001 |
| Total matched waits | 3.985 |

The principal images are 41 (job key 10, caller `001F6930`, ~1.769 ms wait
per pump frame), 43 (key 18, caller `00697570`, ~1.469 ms), and 30
(key 0, caller `007E5414`, ~0.746 ms). Images 32/33 contribute little observed
waiting here. Total execution summed over all workers is a different quantity;
for example, image 30 executes many jobs concurrently and its aggregate worker
wall time must not be described as a serial frame budget.

| Matched-wait component | Fastest half, windows 0 / 1 | Slowest 1%, windows 0 / 1 |
| --- | ---: | ---: |
| Before SDK submission, ms/frame | 1.016 / 1.039 | 2.640 / 2.499 |
| Worker startup, ms/frame | 0.016 / 0.016 | 0.282 / 0.084 |
| Worker execution overlap, ms/frame | 1.749 / 1.760 | 2.716 / 2.860 |
| After ABI, ms/frame | 0.567 / 0.571 | 0.797 / 1.061 |
| Total matched waits, ms/frame | 3.412 / 3.448 | 6.635 / 6.598 |
| Entire pump interval, ms/frame | 9.939 / 9.957 | 17.061 / 17.131 |

The slowest groups contain 18 frames each. These are descriptive correlations,
not proof that removing a phase saves that amount of frame time. Other costs
also grow: FIFO time increases from ~3.32 ms to 4.64–5.37 ms.

## Next replacement target

QPC containment on the same host thread places image-41 waits in `005FBB70`,
image-43 waits in `00697C70`, and image-30 waits in event-2 `00684ED8`.
The first callback averages ~4.52 ms; its completed depth-zero job loops spend
~1.75 ms in the delay wrapper and ~0.018 ms in the completion predicate per
callback. `00697C70` averages ~1.57 ms, including ~1.45 ms in the delay wrapper.
Neither path records calls to the profiler's `service` stage (`00723198`);
pending checks are only ~0.0005–0.0006 ms per callback. Source inspection shows
this stage is notification coordination, not submission dispatch. The actual
completion predicate can drain completion records before the delay.
Delay-wrapper time includes waiting while
jobs execute, so it is not all removable sleep overhead.

Investigate the separate route-2 dispatch path and its readiness/wakeup policy
before SDK submission, alongside native replacements for images 41/43. The
observed pre-submission wait is the larger scheduling lead; merely expanding
worker reuse again does not target it. Any change needs ordinary-build A/B
measurements with median, average and tail frame times, plus gameplay checks.
No new runtime policy is promoted by this diagnostic capture. Consistent
pacing and unlocked/VSync acceptance remain open.

### Submission-consumer source audit

The route-2 title path calls `0071F788`, which makes a blocking
`cellSpursQueuePushBody` call. SDK `libs/spurs/cellSpursQueue.c` waits only
when its header is busy or the queue is full; `spurs_queue_wait.h` ordinarily
uses `Sleep(1)` for that wait. A successful push signals a registered task
waiter after publication and unlock; it does not directly launch the async job.

`runtime/spu/spu_taskset_dispatch.c::spu_taskset_start_ready` scans task IDs,
claims eligible guest ready/pending-ready tasks under the lock, then resolves
the image and calls `spu_workload_dispatch_async_task` outside the lock.
There is no sleep inside this launch scan. Task creation and task exit call
it directly. A separate `libs/spurs/cellSpurs.c` fallback kernel loop calls
`spu_taskset_poll` after **`Sleep(1)` on every iteration**, including discovery
of siblings created by persistent tasks. `spu_taskset_signal_task` latches a
signal and wakes the signal condition variable, but does not itself invoke
the ready-task scan.

This yields a concrete replacement candidate: wake submission consumers on
real queue/task readiness instead of relying on the 1-ms discovery poll.
First distinguish queue backpressure from ready-task discovery by capturing
the successful launch trigger (create, exit or poll), taskset/task identity,
and queue busy/full waits against the existing SDK-submit timeline. Existing
pre-SDK totals do not establish which of these paths dominates. The measured
join is for the same waited-on job generation; unrelated later submissions
or main-loop ordering cannot explain it by themselves. Preserve claim
ownership, readiness predicates, completion ordering and fallback coverage
when prototyping an event-driven consumer.

## Dispatch provenance capture

SDK `e816b27bacf71f64e527a1c5661cbb114aa3b404`, integrated by root
`8da95bc4daeadc1fdda02ea7da3c22d4a606d6b2`, tags successful task claims as
creation, task-exit, or fallback-poll launches. The same immutable SDK job ID
carries claim QPC through submission; new kind 16 spans claim-to-submit.
Ordinary builds do not query QPC, and the diagnostic callback must be installed
before workers start. Existing readiness/claim order is preserved.
Tests pass: taskset dispatch contracts, 40,000 concurrent create/exit cycles,
nine job-trace analyzer cases and four wait-to-frame cases. A two-job diagnostic
build succeeds. Discovery endpoint/TID and job identity mismatches are rejected
by the analyzer; unknown/missing provenance is explicit.

Candidate `local/candidates/dispatch-provenance/HazeRecomp.exe`:

- EXE SHA256: `E138F08107395A2E5D2AD6FD079C577EC2BDDB4C255F6626AB8CADB70C02F45E`
- PDB SHA256: `53410BB95DBF5F58AEF05172B9B3B952FD34B33CDE1342BB8B589D07EF577507`
- Run `20261014-030101-887`, PID 37424; prefix
  `local/jungle-dispatch-provenance-a1-*`.
- Same private baseline/config/cache procedure and fixed modes as above;
  muted, boot reporting off, tutorial completed/Nectar drained, PAD detached.
- Two 15-second sweeps, tokens 3820/3821, 10 MHz QPC:
  `14591055573580..14591205576521` and
  `14591255589390..14591405594401`.
- 2,704 present intervals: **90.209 FPS, mean 11.085 ms, median 10.864 ms**,
  p95 13.897, p99 16.233, maximum 20.658; zero over 33.333 ms.
- Camera/mode/process checks pass; nine resource samples bracket both windows,
  maximum gap 5.341 seconds, memory 54.55–54.78%, no observed compiler/other
  Haze. No owned build/capture/heavy analysis overlaps timing. Normal exit zero;
  all seven immutable save hashes unchanged.
- All 2,097,152 retained job events are consecutive, with zero drops, missing
  tickets/parents or incomplete scopes. Discarded cutoff `14590910081226`
  precedes both windows. Strict wait-to-frame retention checks pass; zero
  skipped joins in both windows.

| Matched waited-on image | Poll launches / waits | Poll pre-claim wait ms/frame, windows 0 / 1 | Poll claim-to-submit wait ms/frame, windows 0 / 1 |
| --- | ---: | ---: | ---: |
| 41 | 20,840 / 21,648 (96.3%) | 0.557 / 0.566 | 0.046 / 0.045 |
| 43 | 2,558 / 2,715 (94.2%) | 0.612 / 0.597 | 0.030 / 0.033 |
| 30 | 2,431 / 2,795 (87.0%) | 0.036 / 0.030 | 0.003 / 0.002 |

Remaining matched waits have task-exit launch provenance. These results confirm
that the fallback polling path claims most waited-on jobs in these images;
they do **not** prove all pre-claim time is scheduler delay. It includes time
before a task is ready, work performed by the consumer, and descheduling.
Claim-to-submit preparation is comparatively small for images 41/43.
No timing difference from other instrumented captures is treated as a gain.

Track the native readiness-wakeup replacement separately in **#94**. Its next
measurement must observe actual eligibility publication and distinguish
not-ready time from ready-to-claim latency. Preserve the current readiness
predicate, claim ownership and bounded fallback, then use ordinary-build
OFF/ON measurements before promotion.
