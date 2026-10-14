# Producer-thread work at the jungle checkpoint

Tracking: #38 (native memory operations), #39 (job waits), #35 (command
submission), #71 (stall attribution), with parent #32. This diagnostic selects
concrete follow-ups; it does not measure a replacement's speedup.

## Capture and perturbation

Muted private-save run `20261014-004946-585`, PID 47988, uses the existing
ordinary `deferred-texture-logs` binary, code Haze `a11df11` / SDK `f6cba5c`,
SHA256 `FAEAF863B41475F3206A5B3E1A7BA0B341F95797223E0BDC23704FA4B4A430CF`.
The real jungle checkpoint and settled view are visually checked after the
tutorial. Render resolution is 2560x1440, shadows 8192, FXAA/AF16, VSync off,
uncapped. CPU 13571, GPU occurrences 2, VP burst 1, worker reuse on; native
job-wait, inline-read/coherency, slot fill, milestone and slow-renderer probes
off. The save, graphics file and shader-cache bundle are private copies.
Diagnostic input detachment is acknowledged before camera token 3660.

The startup frame-pacer mode record identifies producer host TID **5076**.
`scripts/profile_native_cpu.py` samples only that verified thread lifetime for
60 seconds at randomized mean 13-ms intervals, with 32-frame native unwinds
every round. Matching PDB setup precedes sampling. The exact command is:

```powershell
./.venv/Scripts/python.exe scripts/profile_native_cpu.py 47988 60 --tid 5076 --stack-depth 32 --stack-every 1 --interval-ms 13 --raw-limit 200 --stack-limit 500 --symbol-path local/candidates/deferred-texture-logs --timeline-output local/jungle-producer-steady-a1-timeline.json
```

QPC capture: `14513082066643..14513682142565`, frequency 10 MHz. All **3,903**
samples are retained with no overwrite, invalid context or empty stack.
1,251 reach the 32-frame depth limit, so deeper callers can be absent.
Maximum intersample gap is 78.273 ms. Measured thread-suspension brackets total
**5,784.790 ms**, maximum **5.303 ms**; this materially perturbs execution.
Do not subtract the suspension total to manufacture an unperturbed FPS result.

`GetThreadTimes` reports **50,750 ms of producer CPU time** over 60.008 seconds
(about 0.846 core equivalents), versus **236,421.875 ms** process-wide.
This establishes substantial main-thread CPU activity during this diagnostic.
It does not partition CPU between sampled locations or show how much of the
renderer's command-wait time is recoverable. Kernel/yield loops can consume CPU;
stack counts, including sleeping states, are not CPU percentages.

## Repeated paths

Counts below include a function at most once per sample, even if it recurs in
the stack. Different rows can overlap and must not be added as a partition.

| Function/path in sampled stack | Samples |
| --- | ---: |
| Guarded job-delay wrapper `haze_job_wait_call` | 1,418 |
| `sys_timer_usleep` | 1,415 |
| `vm_read32` | 473 |
| `vm_write32` | 293 |
| `vm_write64` | 261 |
| FIFO drained-EA wait `gcm_fifo_wait_for_ea` | 162 |
| SPU reservation predicate `spu_coh_is_reserved` | 112 |

All 1,415 sampled usleep stacks go through the existing completion loop. The
caller families after `func_00682538` are:

| Caller chain prefix | Samples |
| --- | ---: |
| `001F68B0 -> 001F6C00 -> 00264848` | 443 |
| `00697550 -> 00697C70 -> 00680FA0` | 430 |
| `007E53E0 -> 0072BAD0 -> 006740E0` | 304 |
| `001F68B0 -> 001F6C00 -> 0023A420` | 238 |

These are the known 330-microsecond job-delay family, distinct from the REF
equality wait investigated separately. Earlier native-delay policies had mixed
results; this recurrence alone does not justify another identical A/B or
default promotion. No `sys_cond_wait` appears in these stacks.

## Selected follow-ups

1. **Native slot payload copy (#38).** At LR `00682798`, `func_00682648`
   copies exactly `0x1410` (5,136) bytes from its saved input pointer to a
   newly acquired job slot plus `0x10`, after the existing `0x1450` zero-fill
   and before later job publication. Of 36 memory-helper samples under the
   general `func_00C07078` copy routine, 29 have this immediate guest caller.
   The translated copy is a large alignment/overlap implementation using
   scalar VM helpers. Audit a bounded native copy at this exact callsite,
   with source/function hashes, normal-memory/reservation/diagnostic guards,
   preserved continuation behavior and original-path fallback. Keep zero-fill
   and copy separate; merging them can change source data when spans alias.
   No millisecond saving is established by these counts.
2. **Measure producer FIFO recycling (#35/#71).** `cellGcm_fifo_recycle`
   first waits for consumption up to the current command pointer, then for
   acknowledgement of the wrap to the beginning. The current slow-drain
   recorder measures the consumer walker, not these two producer-side waits.
   Add bounded deferred per-callback records for both acknowledgements and
   correlate their QPC intervals with the pump before replacing their contract.

Other frequent memory callers are the already optimized VP producer, scattered
register saves, or smaller interleaved operations with unproven overlap rules.
They do not justify a broad guest-memory rewrite from this capture. The previous
inline-read and zero-timeout experiments remain preserved, not rerun merely
because helper/wait stacks recur.

## Diagnostic frame context

All 4,894 complete present intervals average **12.259 ms / 81.570 FPS**, p95
15.598 ms, p99 17.918 ms, maximum 23.023 ms; no interval exceeds 33.333 ms.
These are heavily sampled diagnostic frames, not the latest ordinary timing
baseline. Pump means: 6.587 ms command wait, 3.746 ms FIFO, 1.853 ms present.
The worst retained pump frame, 57366, takes 23.023 ms and includes one
`vm_write32 -> 006B74F8 -> 00697588 -> 00697C70 -> 00680FA0` sample, with
0.904 ms observed suspension. One location cannot explain the entire frame.

Camera/mode/present and sampled process gates pass. Thirteen resource samples
bracket timing, maximum gap 5.324 seconds, memory 52.35–52.68%, no other Haze
or compiler observed. No owned build, screenshot or heavy offline analysis
overlaps sampling. The process exits normally with code zero and all seven
immutable save hashes still match. Evidence prefix:
`local/jungle-producer-steady-a1-*`, including complete timeline, CPU counters,
all frame intervals, aggregates, resource observations and QPC correlation.

The latest separate counter-only camera diagnostic is 93.387 FPS / 10.708 ms,
p99 15.554 ms, max 51.333 ms; see the REF activity document for its setup limits.
Neither run verifies consistent frame pacing or 144 FPS, and neither changes
the user WIP executable.
