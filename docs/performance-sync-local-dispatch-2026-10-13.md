# Synced performance branch and local dispatch repair, 2026-10-13

Related: #78 (dispatch correctness), #39 (job waiting), #71 (graphics stalls), #32 and #9 (performance/pacing).

## Source synchronization and repair

Local main is `4e498ec` with canonical SDK `haze` at `e6b28a0`; the clean
main-integration checkout has the matching submodule. The experimental root
merged main in `09dac54`. SDK merge `cbde391` integrates canonical audio,
vectorcall arithmetic, overflow-safe texture upload preflight and checkpoint
fixture fixes while retaining experimental controls and diagnostics. SDK local
`haze` also points to its canonical remote tip.

SDK `1e1d13b` fixes jump-table discovery beyond an out-of-function tail-branch
epilogue. In-function branches and unknown function extents remain conservative.
All 12 behavior vectors plus the encoding check pass. Bounded original-ELF
discovery now finds all nine destinations of dispatch `0x29788`, within
`func_000296A0`. The original missed discovery caused the `0x2984C` unresolved
call/abort in issue #78; do not register its cases as independent functions.

Root `5f4b64c` applies a hash-guarded build-local repair to the existing lift:
a local CTR switch and labels on the nine already-emitted bodies. The original
private generated source stays untouched. Unknown-target call/return behavior
remains. The extracted actual-function fixture checks all nine callee arguments,
LR, active stack, shared epilogue restoration, exact tail trampoline, unknown
CTR fallback, index 9, source-hash rejection and byte-for-byte reversibility of
all edits. Both tests pass with clang-cl.

An ordinary two-job build passes. The complete regression suite was executed
across the initial run, a rerun, and the remaining test tail after fixing two
missing renderer frame-counter stubs in the scaled-blit and FIFO fixtures
(SDK `03571e1`). These were link-only fixture failures; no production code was
changed for them. GPU/readback, input/menu, audio, native arithmetic, camera,
queue, dispatch and runtime tests pass. Logs are private under
`local/synced-local-dispatch-{build,tests,retest,test-tail}.log`.

Tested root `5f4b64c`, SDK `03571e1`; candidate `synced-local-dispatch`:
`35BB810181F25B9CA5881BC0227BE4DC6E5E4683DBEC16C31F1BC4985A6E969A`.
This includes broader experiments and is not the smaller merged checkpoint binary.

## Corrected-camera queue comparison

Both launches use independent copies of the immutable real-save jungle baseline,
visually verified after dismissing the Nectar tutorial. Fixed 2560x1440, 8192
shadows, VSync off, uncapped, GPU mode 2, CPU mode 13571, VP consumer on, native
producer off. Muted output retains the audio pipeline. Diagnostic PAD_FILE is
acknowledged detached before two 60-second absolute camera sweeps with five-second
warmups. No own compiler, screenshot or stack sampling runs during timing.
Every timed frame is retained. All seven baseline save hashes still match.

| Mode | Frames | FPS | Mean ms | p95 ms | p99 ms | Max ms | >33.333 / >50 ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Native queue wake | 10,424 | 86.89 | 11.509 | 14.909 | 17.690 | 1640.001 | 6 / 4 |
| Original delay | 10,198 | 85.00 | 11.765 | 14.806 | 16.763 | 62.904 | 2 / 2 |

Native run `20261013-181949-183` (PID 29560, tokens 2980/2981) and original
run `20261013-182508-154` (PID 40876, tokens 3000/3001) both exit normally,
without timeout or the unresolved local-dispatch abort. Actual camera, mode and
present continuity checks pass. This is checkpoint coverage, not full-campaign
or transition acceptance.

These are shared-load diagnostics, not a causal speedup or release acceptance.
Native timing overlaps up to two compilers and another Haze (PID 46416,
menu-acceptance), with memory 46.87-48.52%. Its process-observer gate also fails
because one gap is 5.010 s. Original timing has no observed compilers but overlaps
another Haze (PID 35072), with memory 47.95-48.62%; its isolation gate fails too.
The strict failures are preserved separately from diagnostic reports. Resource
observations bracket the windows, with maximum gaps 5.357 / 5.373 s.

Process CPU averages 4.061 / 4.218 cores in the native blocks, versus
4.097 / 3.922 in the original blocks. This is whole-process CPU, not main-thread
utilization. Whole-launch native counts: 476,627 handled, 360,559 queue-ready,
116,068 deadline-expired, zero fallback, zero primary-slot ready; original guest
queue service still owns completion.

## Remaining bottlenecks

Mean pump phases (native / original) are outer command wait 5.429 / 6.344 ms,
FIFO processing 4.050 / 3.506 ms, presentation 1.951 / 1.831 ms. These are elapsed
pipeline phases, not independent CPU costs or guaranteed removable wait time.
The slightly better native mean does not establish repeatable improvement;
its tail behavior is worse. Native waiting remains off by default.

Native frame 37506 takes 1640.001 ms: 1637.132 ms in FIFO processing over three
drains, 1.049 ms outer wait, 1.819 ms presentation. This capture has no per-drain
ring, so do not call it one single 1637-ms drain. Frame 38339 takes 574.336 ms:
562.240 ms presentation, 4.505 ms FIFO, 7.583 ms outer wait. Coarse thread CPU is
15.625 ms in each frame, which alone cannot distinguish GPU waits, scheduling,
or other blocking calls. Both rare paths need finer attribution.

Stable 144 FPS needs 6.944 ms per frame and remains unachieved. Next diagnostic:
40-ms triggered render-thread stacks alongside bounded FIFO and flush-phase
rings, using the same binary and the original job-delay policy. No default
promotion or new release option follows from this comparison.

## Triggered render-thread follow-up

Run `20261013-183037-612` (PID 53284, render/window TID 40944) uses the same
binary, original delay, 5-ms flush threshold, and 2-ms FIFO threshold. A fresh
copied jungle scene was visually checked before camera token 3020. The 90-second
window spans QPC `14284199747587..14285099787642` at 10 MHz. It retains 8,464
present intervals: **94.04 FPS / 10.633 ms mean**, p95 13.358 ms, p99 15.143 ms,
maximum 20.254 ms, zero above 33.333 ms. Mean pump phases: outer wait 5.580 ms,
FIFO 3.194 ms, presentation 1.785 ms.

Camera, actual-mode, continuity and process gates all pass. No other Haze or
compiler is observed during the window; memory is 47.83-48.32%, with 18 resource
samples bracketing timing and a 5.331-second maximum resource gap. This is a
separate diagnostic baseline, not a matched isolated queue-wake speedup.

The 40-ms trigger records zero episodes, zero samples, zero measured thread
suspension. Its maximum polling gap is 66.725 ms, so the trigger alone cannot
prove absence of hitches; complete present/pump intervals independently show no
long hitch in the window. This unsuccessful reproduction cannot identify the
blocking call behind the earlier 1.64-second or 574-ms frames.

The FIFO ring retains 16,384 of 18,070 events (1,686 overwritten); 7,818 overlap
timing. The slowest in-window drain is 17.815 ms, with 0.0001 ms lock wait and
no overlapping retained flush. The flush ring retains 4,096 of 6,372 events
(2,276 overwritten); none overlaps timing. Both retained extents bracket the
window, but sparse thresholded rings are not continuous tracing. Before the
timed window, retained frame 12448 takes 89.528 ms including 77.714 ms in
presentation; no retained flush overlaps it. It is preserved and not included
in the later window's statistics or attributed to a particular cause.

The run closes normally (exit 0, no timeout), and all seven immutable save
hashes still match. No source/default change follows from non-recurrence.
Remaining work is to capture the rare blocking call, reduce the ordinary
producer-wait/FIFO costs, and demonstrate repeatable gains toward 6.944 ms.
