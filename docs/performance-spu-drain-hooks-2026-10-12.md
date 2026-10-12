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

## Ordinary-build frame-time comparison

Two independent muted gameplay runs used the same copied checkpoint, stationary
carrier view and 1440p/follow-shadow/AA-off/VSync-auto/30-cap configuration.
Native PPU queue waits and native SPU idle waits were enabled in both builds,
confirmed by their worker mode events. Each run recorded two 60-second blocks
after five-second warmups, with screenshots only before and after measurement.
There was no instruction sampling, renderer profiling, job logging, building
or injected input during the blocks. Native present IDs are consecutive.

| Build / block | Frames | FPS | Mean ms | p95 ms | p99 ms | Max ms | Mean CPU cores |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Outlined 1 | 1230 | 20.52 | 48.74 | 71.14 | 89.44 | 265.50 | 3.92 |
| Outlined 2 | 1440 | 24.01 | 41.65 | 55.88 | 63.94 | 74.28 | 4.43 |
| Historical 1 | 901 | 15.03 | 66.53 | 104.57 | 143.98 | 234.43 | 3.22 |
| Historical 2 | 953 | 15.90 | 62.91 | 96.27 | 126.31 | 226.43 | 3.36 |

Outlined run `20261012-092700-059` used the SHA256 above. Captures 7171 and
10754 checked the scene; host idle-worker TID 21444 supplied settled mode
evidence. It exited normally at 09:34:16 UTC. Historical run
`20261012-093437-395` used SHA256
`FE915652B8302B4A2BE81EC8E3F31A1E66D025DAF176BB4EDD68C02F40737E95`.
Captures 7641 and 10373 checked the scene; its worker was TID 47716. It exited
normally at 09:42:38 UTC.

Artifacts are `local/drain-outline-{windows,isolated-report}.json`,
`local/drain-baseline-{windows,isolated-report}.json`, the corresponding
`*-presents.csv` and `*-processes.jsonl` files, and both run directories.
The reports now accept `--process-log` and reject another observed Haze,
missing owned process, wrong executable, missing bracketing observations or
observation gaps longer than four seconds. These runs each have 30 observations
per block, maximum gaps 2.12–2.30 seconds, with only their owned game observed.
This sampling cannot rule out a process entirely between observations or
contention from other applications.

The new build performed better in this ordered comparison, but two independent
sessions with live NPCs do not isolate a repeatable percentage gain. The older
build has also reached about 22.5 FPS in earlier native-idle windows. The new
build's own windows differ substantially, and its first window contains a
265 ms stall. Do not interpret lower process CPU in the historical run as
greater efficiency: it also completed fewer frames. Consistent 30 FPS and
correct unlocked simulation timing remain unverified.

A reverse-order repeat, `20261012-094407-373`, exited with `0xC0000005` at
09:45:10 UTC before any gameplay measurement. Its final log shows title/attract
playback teardown and mixer startup. Host TID 58596 attempted a write to
address `0x24`, with fault RIP `0x00007FFF2F58FA7D`, outside the executable.
The last HLE marker was `sys_ppu_thread_get_id`; that marker alone does not
identify the crashing call. The current crash context records the executable
base rather than the faulting module's base, so its reported `rip_offset`
cannot symbolize this fault. This failed repeat contributes no FPS evidence.
Its relation to the refactor or pre-existing startup races is unproven;
preserve the run and investigate the real fault stack before attributing it.

Validation of the added observation gate: `tests/test_present_modes.py` passes
three tests, including accepted BOM-prefixed JSONL and rejection of missing
coverage, large gaps, duplicate timestamps, additional or absent Haze instances,
wrong PID, unavailable executable path and mismatched QPC frequency. Existing
mode-event, missing-present and stall-retention checks still pass.
