# SPU transfer-hook CPU cost, 2026-10-12

## Measured lead

After the opt-in native idle wait removed most of one persistent SPU worker's
busy polling, ordinary gameplay CPU attribution still missed many short-lived
SPU jobs. A targeted sampler used actual image-40 job-start thread IDs, checked
ownership and thread descriptions, and sampled those live jobs for 20 seconds.
Runtime image 40 is generated `spu_0039_at_00F1C100`, not generated image 0040.

Baseline run `20261012-084119-545` used executable SHA256
`FE915652B8302B4A2BE81EC8E3F31A1E66D025DAF176BB4EDD68C02F40737E95`.
The profile `local/spu-idle-image40-samples.json` opened 281 of 287 announced
jobs and collected 1275 samples. Leading locations included:

| Location | Samples |
| --- | ---: |
| spu_task_launch_check | 218 |
| image-40 function 3450 | 175 |
| spu_drain_call | 83 |
| image-40 function 35B4 | 83 |
| ldexp | 68 |
| yz_lockstep_tick | 57 |
| spu_check_stack_reset | 48 |

These are instruction-pointer observations for one image, including runtime
helpers and waits. They are not percentages of whole-process CPU or an FPS
measurement. Another Haze instance was active. The recurring launch hook was
a concrete framework cost to investigate without rewriting guest arithmetic.

## Change and correctness

`runtime/spu/spu_drain.c` now keeps optional exit/step tracing, policy flow
recording and opt-in ring recovery in a cold out-of-line function. The ordinary
public hook keeps the same recent-PC history write, stack-reset check, exact
step increment and LS-zero completion rules. No guest work or interrupt checks
are removed. Configured diagnostic environment variables select the original
full behavior; the policy flow recorder is also checked live on each call so
it can arm after startup. An atomic cached integer publishes only a complete
startup probe decision to concurrent first callers.

Framework commit: `83c3147` on the SDK fork's `haze` branch.

The new SDK `test_drain_hooks.c` runs in five separate modes: default, no-LS0,
exit trace, step/register trace and job drain. It checks full context state,
one stack check per call, history order, dynamically armed/disarmed recording,
first-entry versus real LS-zero completion, policy/image exceptions and the
real 4096-iteration recovery threshold. It is part of `scripts/build.ps1 -Test`.

The full build/regression gate passed with:

```powershell
./scripts/build.ps1 -SpuFma -Test -OutputDirectory local/candidates/drain-hook-outline
```

Build log: `local/drain-hook-outline-build-tests.log`. Candidate SHA256:
`8711EE6BD420C6FF4895549C9D911906F7D49FD559887FC2C6B7B79E6BF31632`.
This is an ordinary build, renderer profiling off, FMA on, byte comparison off.

## Controlled local benchmark

`tests/benchmark_spu_drain.c` calls both the actual current hook and the
unchanged historical hook in one process on one allowed logical processor.
Only this benchmark thread is pinned; game affinity is untouched. Each window
uses five million calls, a shared noinline stack-reset spy, thread cycle time
and QPC. It verifies exact history, step and stack-call counts. The order is
old/new/new/old/new/old/old/new, repeated twice.

The historical hook is extracted from SDK commit `b582819`; only its public
symbols are renamed to link both versions. From a Visual Studio x64 developer
shell with clang-cl available, at the repository root:

```powershell
.venv/Scripts/python.exe scripts/prepare_spu_drain_baseline.py --output local/drain_cost_baseline.c
clang-cl /nologo /W0 /O2 /Gy /Gw /clang:-msse4.1 /Iexternal/ps3recomp/include /Iexternal/ps3recomp/runtime/spu /Felocal/benchmark_spu_drain.exe /Folocal/ tests/benchmark_spu_drain.c local/drain_cost_baseline.c /link /OPT:REF
local/benchmark_spu_drain.exe
```

| Measurement | Original cycles/call | Outlined cycles/call |
| --- | ---: | ---: |
| First controlled run | 18.17–22.65 | 10.81–14.26 |
| Checked-in reproduction | 24.44–25.70 | 14.39–15.70 |

Artifacts: `local/drain-hook-combined-cost.jsonl` and
`local/drain-hook-reproduced-cost.jsonl`. All windows ran on logical CPU 0;
each new window was cheaper than every old window within the same run.
CPU frequency, system contention and code layout can affect absolute costs.
The shared spy does not execute the real scheduler. This establishes a local
hook-cost reduction, not a measured whole-game FPS gain. Isolated gameplay
timings, the slow squad conversation and unlocked simulation timing remain
outstanding.

## Startup observation

Run `20261012-090328-153` sat at the title screen for over ten minutes before
advancing to a blank profile without any save-autoload calls. A similar case
had occurred with the pre-refactor executable. Fresh run
`20261012-091641-780` loaded both profile files normally and enabled Continue
Campaign. The long-title anomaly remains unexplained and is not evidence
that the new hook caused a save regression. Automation uses isolated copied
saves and checks each menu before sending input; short poll-count button
pulses sometimes need retrying.

The fresh run then loaded Continue Campaign and reached the carrier, verified
by capture `requested_010826`. It used the same 2560x1440/shadows-follow/AA-off/
VSync-auto/30-cap config, native PPU queue wait on, native SPU idle wait on,
muted audio with the real pipeline retained, and image-40 timing enabled.
Another agent's Haze instance was running, so this supplies gameplay smoke
coverage rather than isolated frame timings.

`local/drain-hook-outline-image40-samples.json` recorded 941 samples over
20 seconds, opening 385 of 430 announced jobs. Function 3450 led with 156
samples; launch check had 106, function 35B4 had 60, drain call 59,
stack-reset check 54, ldexp 52, and lockstep tick 40. Different job lengths,
opening success and concurrent load prevent treating the baseline and new
counts as a controlled percentage speedup. Remaining framework hooks and
the guest arithmetic loop remain leads for further profiling.

The fresh run closed normally with exit 0 at 09:25:11 UTC; the long-title
run also closed normally at 09:16:27 UTC. The bounded runner deliberately
leaves its broad `gameplay_validated` claim false. The carrier capture above
is a visual smoke check, not campaign completion or a correctness guarantee.
