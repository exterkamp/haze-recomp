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

No default promotion is inferred from this diagnostic pair. Track remaining
acceptance in #94; overall frame-pacing and 144 FPS goals remain open.

## Ordinary-build comparison

Built with `scripts/build.ps1 -SpuFma -Jobs 2 -OutputDirectory
local/candidates/taskset-wake-ordinary`; actual compile rules verify renderer
profiling **off**, job profiling **off**, inline reads **off**. Runtime producer
and asynchronous job traces are disabled. Present/pump timing and the same
light process/resource observers remain enabled in both modes.

- Root build identity: `1a1fa74b616c3a19fd9745dc4fdbfa741f39234d` (same code as
  `f009524`, documentation only afterward); SDK unchanged at `6596635`.
- EXE SHA256: `BDB3FF044D4DB6CBF90AA024D68F0327AB05086C77E7F9A8C502458CAAE4C7D8`.
- PDB SHA256: `C6302F2826BEE090264039F302CB7E8BEA737CC9F59B251EC5F67C7F9C55FFA9`.
- Prefixes: `local/jungle-taskset-wake-ordinary-{off,on}-a1-*`.
- OFF run `20261014-033945-659`, PID 7324; ON run `20261014-034521-701`, PID 64496.
- Same 1440p/FXAA/AF16/8192-shadow settings and frozen private save/cache
  procedure. Muted, uncapped, VSync off, native completion-wait replacement
  off, no boot report/milestones. Actual wake mode confirmed in startup logs.

Order is **OFF then ON**, reversing the diagnostic pair. Two 45-second sweeps
per launch, with 5-second warmups; QPC frequency 10 MHz:

| Mode/token | Start QPC | End QPC |
| --- | ---: | ---: |
| OFF/3880 | 14614056041099 | 14614506042785 |
| OFF/3881 | 14614558923281 | 14615008925404 |
| ON/3900 | 14619990512063 | 14620440516788 |
| ON/3901 | 14620490527481 | 14620940528879 |

| Mode | Present intervals | Average FPS | Average ms | Median ms | p95 ms | p99 ms | Maximum ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| OFF | 8,162 | 90.706 | 11.025 | 10.760 | 13.917 | 16.017 | 32.094 |
| ON | 10,182 | 113.152 | 8.838 | 8.557 | 11.246 | 13.203 | 23.650 |

Observed average decreases **2.187 ms (19.8%)**, median **2.203 ms (20.5%)**,
p99 **2.814 ms (17.6%)**; FPS increases **24.7%**. All observed frames remain
in the result, including the 32.094 ms OFF and 23.650 ms ON maxima. Neither
capture has a frame over 33.333 ms. Within-launch window means are
11.021/11.028 ms OFF and 8.811/8.865 ms ON; medians 10.750/10.772 OFF and
8.538/8.581 ON. The fresh OFF control is slower than the older 95.559 FPS
ordinary baseline, so the improvement uses this matched control, not the old
baseline or the diagnostic executable as its denominator.

Both captures pass complete presents, actual CPU/job-wait mode, actual camera
and sampled process checks. Each has 20 resource samples fully bracketing the
windows, no observed compiler or other Haze; maximum sampling gap 5.326 s OFF
and 5.355 s ON. Physical memory ranges are 54.38-54.77% OFF and 52.89-53.53% ON.
No owned builds/screenshots/offline analysis occur in the measured windows.
Both games close normally with exit zero; all seven immutable baseline hashes
remain unchanged. These observations do not rule out all system-wide load or
clock variation.

The first automated ON navigation did **not** reach gameplay: the untimed
capture `requested_119299.ppm` showed the title screen. No performance window
was recorded there. The same process was navigated manually through Continue
Campaign, the same tutorial input sequence was applied, and untimed capture
`requested_207900.ppm` confirmed the matching jungle view with an empty Nectar
gauge. PAD polling was then acknowledged detached before timing. Keep this
navigation failure and extra menu warmup in the record.

Mean pump elapsed scopes, OFF to ON: wait **5.494 -> 3.627 ms**, FIFO
**3.625 -> 3.342 ms**, presentation **1.840 -> 1.813 ms**. Most observed gain
is in waiting, consistent with the diagnostic dispatch evidence. These are
elapsed scope times, not additive CPU costs across threads.

The ordinary result corroborates the diagnostic improvement, but it is still
one launch per mode in a live scene. Repeat launch pairs and wider gameplay,
checkpoint reload, squad conversation, transitions and lifecycle/failure-path
acceptance remain before default promotion. The internal control remains off
by default. At **8.838 ms average / 13.203 ms p99**, stable 144 FPS
(6.944 ms/frame) and consistent pacing are still unachieved.

## Reverse-order repeat and pooled results

The same ordinary executable was tested again in reverse order, **ON then
OFF**, with two 45-second sweeps per launch. Prefixes
`local/jungle-taskset-wake-ordinary-{on,off}-b1-*`; ON run
`20261014-035910-701`, PID 53020, OFF run `20261014-040602-325`, PID 61756.
The private navigation helper used a 60-second initial menu delay instead of
35 seconds; both runs reached the intended scene automatically. Captures
visually confirmed empty Nectar gauges before PAD detach and measurement.

| Mode/token | Start QPC | End QPC |
| --- | ---: | ---: |
| ON/3920 | 14626382123082 | 14626832131117 |
| ON/3921 | 14626882145905 | 14627332150074 |
| OFF/3940 | 14630033944823 | 14630483946500 |
| OFF/3941 | 14630533963213 | 14630983967833 |

QPC frequency remains 10 MHz. Both repeat runs pass camera/mode/process and
complete-present checks. Twenty resource samples per run bracket both windows;
maximum gap 5.371 seconds ON and 5.351 OFF, memory 53.23-53.80% ON and
53.51-54.04% OFF. No observed compiler/other Haze. The integration fixture
compiled before ON timing; there were no owned builds, screenshots or offline
analyses in either measured window. Both exit normally with code zero and
the immutable save hashes remain unchanged.

| Launch | Present intervals | Average FPS | Average ms | Median ms | p99 ms | Maximum ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| OFF A | 8,162 | 90.706 | 11.025 | 10.760 | 16.017 | 32.094 |
| ON A | 10,182 | 113.152 | 8.838 | 8.557 | 13.203 | 23.650 |
| ON B | 10,428 | 115.889 | 8.629 | 8.370 | 13.124 | 20.265 |
| OFF B | 8,383 | 93.166 | 10.734 | 10.505 | 15.665 | 33.190 |

Pooled values below are recomputed from **all individual present intervals**,
not averages of medians or instantaneous FPS. Every frame is retained.
Private artifact: `local/taskset-wake-ordinary-pooled.json`, generated by
`local/aggregate-wake-ordinary.py`, which rechecks frame counts, per-run
mean/median consistency, executable hash, runtime modes, capture validation,
resource coverage and immutable baseline hashes.

| Mode | Frames | Average FPS | Average ms | Median ms | p95 ms | p99 ms | Maximum ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| OFF pooled | 16,545 | 91.936 | 10.877 | 10.639 | 13.751 | 15.894 | 33.190 |
| ON pooled | 20,610 | 114.520 | 8.732 | 8.469 | 11.143 | 13.158 | 23.650 |

Observed pooled improvement: **24.6% more FPS, 19.7% lower mean, 20.4% lower
median, 17.2% lower p99**. The benefit survives launch-order reversal. Four
windows per mode remain a limited live-scene sample, not a deterministic
replay or proof of all-scene behavior. Stable 144 FPS remains unachieved.

## Wake policy and dispatcher integration fixture

SDK test-only commit `6a4960b3ba406cc62282a8d79b89219dabb59ff2`, root test
integration `3cb7436`, adds `test_taskset_wake_integration.py`. The benchmark
executable remains unchanged at the recorded `1a1fa74` / `6596635` identity.
The fixture combines the production watcher block and complete production
dispatcher with verbatim `cellSpurs` mode/configuration/wait blocks. Real
Windows events are used, with injected CreateEvent and wait failures.

Verified contracts:

- Disabled mode and event-creation failure retain the polling path.
- A bounded event timeout returns the poll cause; failed waits also take the
  bounded sleep fallback. A spurious event cannot invent eligible work.
- 1,000 tasks recycle through all 128 IDs: observed readiness precedes claim,
  an unchanged publication does not re-signal, and existing claims prevent
  duplicate dispatch. Task exit clears the actual watcher state.
- A failed asynchronous dispatch releases the claim; with no further edge,
  the fallback poll retries successfully.
- Same address/handle reinitialization clears readiness timestamps, dispatches
  already-ready work and reuses the process-lifetime event. Unregister removes
  watchers without suppressing ordinary ready-task polling.

The fixture respects the production lock contract and passes on the final
source. Both watcher and integration fixtures are now called by
`scripts/build.ps1 -Test`; PowerShell syntax parsing passes. The full build-test
suite was not rerun for this test-only integration. These focused contracts
supplement, rather than replace, actual checkpoint and scene-transition checks.

## Initial in-game lifecycle check

Muted ordinary-build ON run `20261014-041210-902`, prefix
`local/jungle-taskset-wake-reload-c1-*`, used the same executable, private
save/config/cache and wake policy as the comparisons. This was a correctness
run with scripted PAD retained, not a performance sample.

Untimed captures show checkpoint restart returning to the jungle
(`requested_100886.ppm`), a responsive pause menu afterward (`115602`), Quit
returning to the main menu (`136816`), Continue Campaign loading the jungle
again (`190251`) and a responsive pause menu after that load (`198232`). The
restart may reuse cached checkpoint state; the main-menu round trip exercises
scene teardown and loading. A brief accidental visit to Sound Setup was exited
without changing settings. A right-stick command was acknowledged but did not
produce a clearly verified camera change, so this run does not establish
movement/camera correctness.

Teardown logged four `PhysX: Cannot detach SPURS queue (-2147418049)` warnings
and unresolved NID `0x039D70B7`. The SDK's `tools/nid_from_protos.json` maps it
to `cellSpursQueueDetachLv2EventQueue`; source inspection finds an attach
implementation but no detach implementation. This identifies missing SDK
coverage, not a newly introduced wake-policy failure. OFF attribution is still
pending at this checkpoint.

The runner reached its configured **900-second timeout** before the later
window-close attempt. `boot-result.json` records `timed_out=true` and
`exit_code=null`; the attempted close found no matching owned process. Do not
count this as an orderly exit or a demonstrated crash. All seven immutable
baseline hashes still match. This partial lifecycle result does not complete
broader gameplay/conversation/transition acceptance or authorize a claim of
stable 144 FPS; native task wakeups remain off by default.

### Polling control for the teardown warning

Fresh muted OFF run `20261014-043409-647` (PID 53388), prefix
`local/jungle-taskset-wake-reload-off-c2-*`, uses the same ordinary executable
and private 1440p settings. Startup verifies `enabled=0 requested=0 event=0`.
Capture `requested_060427.ppm` shows jungle gameplay (Nectar still active;
this is not a timed comparison). Quit returns to the main menu in `074421`.
The same four PhysX queue-detach warnings and unresolved `0x039D70B7` occur
with wakeups disabled. This warning is therefore also present in the control;
it is not specific to native task wakeups.

The first Continue confirmation sequence returned to the Campaign menu
(`108788`), rather than loading. A retry with the OK selection visually checked
(`123584`) loaded the jungle (`135220`). Preserve this scripted-input retry;
do not count the first attempt as a successful load. The owned window then
closed normally: `boot-result.json` records `timed_out=false`, `exit_code=0`
at `2026-10-14T04:41:02.9440965Z`. No performance claim is derived from this
correctness run.

### Wake-enabled reload and orderly exit

Fresh muted ON run `20261014-044115-692` (PID 43464), prefix
`local/jungle-taskset-wake-reload-on-c3-*`, verifies startup
`enabled=1 requested=1 event=1`. Captures show initial jungle gameplay
(`requested_077176.ppm`), Quit confirmation (`094722`), main menu after teardown
(`105656`), Continue confirmation (`134306`), reloaded jungle (`169514`), and
responsive pause menu after reload (`172752`). Stdout records Shutdown/Join
followed by creation of taskset `0x014E1600` at the same address. The same four
queue-detach warnings occur, as in the polling control. This validates the
observed taskset reuse flow; it is not a proof of every finalize race.

The owned window closed normally before the deadline:
`timed_out=false`, `exit_code=0`, timestamp `2026-10-14T04:50:44.6505691Z`.
The earlier ON timeout remains retained above. These correctness runs add no
new frame-time measurements. Broader squad progression and testing after
integration with current main remain outstanding for the checkpoint PR.
