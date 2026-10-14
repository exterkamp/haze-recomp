# Native taskset readiness wakeups (#94)

The readiness-wakeup prototype replaces the scheduler's unconditional 1 ms
sleep with an auto-reset event wait when enabled. An eligibility edge on a
registered taskset header signals the event. The scheduler still validates
the existing ready/enabled predicate and takes the existing exclusive host
claim. It retains a 1 ms fallback for unobserved writes, watch collisions and
unsupported paths. This is an experimental internal comparison control
(`SPU_TASKSET_WAKE=0/1`), disabled by default pending ordinary-build and wider
gameplay validation. No user-facing graphics or input setting was added.

## Reproducible candidate

- Root: `f009524e019d6db16209094bf801a03bd178c27a`.
- SDK: `6596635e5aeaa19a0fa0882d071aa18212376d14`.
- Candidate: `local/candidates/taskset-wake/HazeRecomp.exe`.
- EXE SHA256: `7668A734833BD257FE0423EC6107231E46925C5E6176249148BF5FF2127139BC`.
- PDB SHA256: `4F4D4D92A7A8099E94B21132289CA78344BA9BCAA5A6AECD7F8307781FE22753`.
- Build: `scripts/build.ps1 -SpuFma -ProfileJobs -Jobs 2 -OutputDirectory local/candidates/taskset-wake`.
- Actual compile rules verified: renderer profiling off, job profiling on,
  inline reads off. Runtime producer/job tracing enabled for attribution.

The watcher uses the lock already held by coherence publication, including
the normal and writer-excluding notification paths. It signals only newly
eligible bits (`enabled & (ready | pending_ready)`), without scanning tasks
under that lock. Task bits are MSB-first. Registration snapshots readiness;
instance reinitialization unregisters old snapshots even if addresses are
reused. Event handles have process lifetime because the SDK's scheduler
threads are permanent. Event creation failure leaves the original poll path.

Diagnostic kind 16 now records observed readiness QPC as well as claim QPC.
Zero readiness QPC means unobserved/already ready before registration, not
zero dispatch delay. Ordinary builds do not query QPC for this feature.
The analyzer retains unknown-readiness intervals explicitly and validates
job identity, dispatch cause, timestamp order and submit endpoint/TID.

## Focused validation

- Actual-source watcher fixture passes using real Windows auto-reset events:
  tasks 0/7/8/127, ready/pending/enabled rises and falls, unchanged/unrelated
  writes, disabled tracing, publication before and during wait, reinit,
  unregister and collision fallback. Both production notify entry points
  are checked for the watcher hook.
- Existing taskset dispatch contract and 40,000 concurrent create/exit
  cycles pass, with watch API stubs asserting the lock contract.
- Ten job-trace analyzer and four wait-to-frame tests pass.
- Diagnostic build succeeds with two compiler jobs. The first build failed
  because the new dispatcher fast-path used Windows types without including
  `windows.h`; the include was corrected and the fixtures/build rerun. The
  failed log remains `local/taskset-wake-build.log`; successful retry is
  `local/taskset-wake-build-retry.log`.

These checks do not establish full gameplay/lifecycle coverage. Wider loading,
squad conversation and transition checks remain required before promotion.

## First ON capture

Prefix `local/jungle-taskset-wake-on-a1-*`, run `20261014-032650-212`, PID 31152.
Same immutable private jungle save and graphics snapshot: 2560x1440, FXAA,
AF16, 8192 shadows, uncapped, VSync off. Muted, boot reporting/milestones off,
fixed GPU/CPU modes, native completion-wait replacement off. Fresh private
copy of the common shader cache. Nectar gauge visually drained before timing,
PAD-file polling acknowledged detached, repeated absolute camera sweeps.

Two 15-second windows, QPC frequency 10 MHz:

- token 3840: `14606362219798..14606512222236`;
- token 3841: `14606562234930..14606712240111`.

3,281 intervals: **109.430 FPS**, average **9.138 ms**, median **8.846 ms**,
p95 **11.733 ms**, p99 **13.625 ms**, maximum **20.907 ms**.
Camera/mode/process checks pass. Nine resource samples bracket both windows,
maximum gap 5.363 seconds, memory 53.96-54.34%, no observed compiler or other
Haze. No owned build, screenshot or offline trace analysis during timing.
Normal exit zero; all seven immutable baseline-save hashes match.

All 2,097,152 retained events are consecutive, with zero missing/dropped
tickets, incomplete scopes or missing parents. Discarded cutoff
`14606319863767` precedes both windows. Strict wait-to-frame checks pass;
zero skipped joins in both windows. Startup confirms enabled=1/requested=1/event=1.

| Image | Wake launches / all matched waits | Before SDK submit, ms/pump frame (windows 0 / 1) | ABI execution overlapping the wait, ms/pump frame (0 / 1) |
| --- | ---: | ---: | ---: |
| 41 | 24,390 / 26,256 | 0.00161 / 0.00090 | 0.608 / 0.605 |
| 43 | 3,256 / 3,283 | 0.02433 / 0.02255 | 0.528 / 0.534 |
| 30 | 2,427 / 2,861 | 0.00139 / 0.00173 | 0.050 / 0.062 |

All wake-launched matched waits in these groups have an observed readiness
timestamp. Small readiness-to-claim intervals are clipped to the actual title
wait: they are not a measurement of whole-job execution or host CPU time.
Moving work earlier can reduce its overlap with the main-thread wait without
making that work itself cheaper. These elapsed intervals must not be summed
as guaranteed removable CPU cost.

## Same-binary OFF control

Prefix `local/jungle-taskset-wake-off-a1-*`, run `20261014-033148-981`, PID 38188.
Identical executable, diagnostic settings, private baseline/config/cache
procedure and camera sweeps; startup confirms enabled=0/requested=0/event=0.
Nectar visually drained before timing. Two 15-second windows at 10 MHz:
token 3860 `14609496608534..14609646613171`, token 3861
`14609696624736..14609846625897`.

| Mode | Present intervals | Average FPS | Average ms | Median ms | p95 ms | p99 ms | Maximum ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| OFF | 2,777 | 92.639 | 10.795 | 10.549 | 13.428 | 15.687 | 20.432 |
| ON | 3,281 | 109.430 | 9.138 | 8.846 | 11.733 | 13.625 | 20.907 |

Observed average decreases 1.656 ms (15.3%); median decreases 1.703 ms
(16.1%), p99 decreases 2.062 ms (13.1%). FPS increases 18.1%. Maximum is
0.475 ms worse and is retained. Neither capture has a frame over 33.333 ms.
This is one ON/OFF launch pair, with two short windows per launch, in a live
scene with independently moving NPCs. It is evidence of a promising change,
not a confidence bound or an ordinary-build release result.

OFF camera/mode/process checks pass. Nine resource samples bracket both
windows, maximum gap 5.366 seconds, memory 54.28-54.55%, no observed compiler
or other Haze. No owned build, screenshot or offline trace analysis overlaps
timing. Normal exit zero; immutable baseline hashes remain unchanged.
OFF also passes strict wait-to-frame retention: 2,097,152 consecutive events,
zero missing/dropped tickets, incomplete scopes or missing parents; discarded
cutoff `14609367869181` precedes both windows.

For image 41, time waiting before SDK submission is 0.572/0.583 ms per frame
OFF versus 0.00161/0.00090 ON. For image 43 it is 0.641/0.678 OFF versus
0.02433/0.02255 ON. OFF launches are still predominantly poll-driven:
21,359/22,240 image-41 waits and 2,660/2,784 image-43 waits. OFF readiness
timestamps are intentionally unknown because the watcher is disabled;
do not interpret them as zero latency. Both windows have zero skipped joins.

The pump's elapsed wait scope decreases from 5.554 to 3.853 ms/frame;
FIFO is 3.355 versus 3.412 and presentation 1.808 versus 1.814. The frame-time
change is consistent with reduced scheduling stalls. Earlier job execution
also reduces image-30 ABI overlap with title waits (roughly 0.59 to 0.056
ms/frame); this is not proof that the job's computation became cheaper.

Ordinary-build OFF/ON acceptance and wider gameplay checks remain pending.
No default promotion is inferred from this diagnostic pair. Track remaining
acceptance in #94; overall frame-pacing and 144 FPS goals remain open.
