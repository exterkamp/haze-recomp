# Timestamped native pump stack capture

The ordinary worker comparison recorded a 597 ms present gap and a 620 ms
pump interval dominated by FIFO wall time, despite no compiler observations
and roughly 47% memory use. Aggregate stack counts could not associate a
particular long interval with the code executing or waiting inside it.

`scripts/profile_native_cpu.py --timeline-output PATH` now retains a bounded
suffix of individual samples with QPC timestamps before suspension, after
context retrieval and after resumption. It records thread identity/lifetime,
context validity, unwind attempts, RIP/call chain and the existing pre-suspend
thread CPU counter. Default capacity is 100,000 samples; overwrite counts are
explicit. Threads resume in `finally`; resume errors are checked. Symbols and
JSON output are resolved/written after capture, not while a thread is suspended.
These diagnostics do not change the game's runtime or introduce a user setting.

`scripts/correlate_pump_stacks.py` validates clocks, sequence, one sampled
thread lifetime, sample ordering and nonoverlapping pump intervals. It matches
context timestamps to fully retained pump intervals and reports sample
locations and overlapping suspension brackets. It does not treat counts as
CPU percentages, subtract suspension time to manufacture ordinary frame
timings, or claim that unsampled parts of an interval are explained.

## Validation and capture

The live optimized native fixture passes with bounded-ring rollover and known
leaf/middle/entry call chains. After review hardening, 292 correct chains were
recovered; maximum suspension 0.591 ms. Three correlation tests cover QPC joins,
ring-prefix exclusion, boundary-crossing suspension brackets, clock/duration
mismatches, NaN, duplicate/overlapping frames and mixed/reordered samples.

Muted disposable-save run `20261013-114156-881`, PID 16044, uses the ordinary
five-workload candidate SHA256
`A6ACBC36AE4C5D9022D2750C8A86DCB783754C3A4A4F373331D1CB0FC7D883A7`.
Same jungle checkpoint, 1440p/8192 shadows, VSync off/uncapped, tutorial cleared
and Nectar activated. Absolute camera sweep token 2260 starts before capture.
Only the owned window thread is sampled (TID 35204, window class
`ps3recomp_null`), with matching PDB, 7 ms randomized target interval, depth 32,
stack walk every second round. The game exits normally afterward.

The 45.006-second capture retains all 5615 samples, zero overwritten; 3692
complete pump intervals fall within its retained extent. Maximum intersample
gap is 67.184 ms. Total measured suspension is 1314.900 ms, max 10.204 ms:
these timings are perturbed and **not FPS benchmark evidence**. Nine resource
observations inside the capture show 47.86–48.10% physical memory use, no
compilers and only this game. Other compilation was observed at launch before
sampling; process/resource sampling cannot exclude all outside activity.

The previous 500+ ms FIFO stall does not recur. The longest retained pump
interval is 39.2868 ms: outer wait 27.4234 ms, FIFO 7.9070 ms, presentation
3.7489 ms. Two unwound samples in that interval show
`NtWaitForSingleObject -> WaitForSingleObjectEx -> frame_clock`, consistent
with its command-arrival wait scope. The interval has four valid context
samples and 2.0214 ms of overlapping suspension brackets. This does not
identify the guest dependency that delayed further commands.

The full capture also contains presentation-fence waits under `ld_flush`,
texture write-watch queries, native blits, vertex packing, content/shader
hashing and controller event polling. Ten unwound samples encounter the
benchmark's VP-mode control-file read. These are observed call paths, not
removable milliseconds. Two 21–24 ms pump intervals have approximately 10 ms
of sampling suspension inside presentation, illustrating why this diagnostic
must remain separate from ordinary comparisons.

The sampler now provides exact interval linkage when a larger stall recurs.
No claim that the earlier FIFO stall is explained or fixed follows from this
capture. Investigating guest/job readiness and the observed 16-slot worker
spill remains justified alongside renderer stalls. Tracking #9, #32 and #36
stay open. Private evidence prefix: `local/jungle-fifo-stacks`, including
timeline, aggregate stacks, correlated pump intervals and resource observers.

## Sampling only after stalled frame progress

The sampler now accepts `--stall-ms 50 --stall-samples 3`, with exactly one
selected thread and a timeline output. It resolves the matching executable's
four-byte `g_frames_presented` PDB symbol and reads it without suspending threads.
In `port/main.cpp`, that counter increments atomically after the backend
presentation for each completed guest frame. The gate arms only after observing
counter progress. After 50 ms without observed progress, it permits at most
three sampling rounds until the counter changes again. Counter wrap rearms the
gate normally. Stalled samples always attempt the requested stack walk.

This is an external diagnostic, not a release/gameplay option. It requires no
game rebuild. Counter polling still has overhead; detecting a stall does not
make subsequent suspensions harmless. The observed progress timestamp can lag
the actual presentation, and the game can progress between the counter read
and suspension. QPC-to-pump correlation, rather than the trigger alone, locates
the sampled frame. The three-sample budget can miss later locations within a
long stall. Initially frozen processes do not trigger without observed progress.

Reports retain poll counts, maximum poll gap, observed progress, episode counts,
per-sample frame counter and trigger timestamps, and suspension costs. Missing
counter symbols, wrong symbol sizes, unreadable counters, absent/exited selected
threads at a trigger, or failed triggered suspension stop with an explicit
error. They do not silently emit a successful empty capture. A successful capture
with no detected stalls is valid and explicitly reports no triggered samples.
The correlator includes complete pump frames across that capture, including the
frame that first triggered sampling; overwritten prefixes remain excluded.

Validation: three deterministic gate tests cover arming, threshold, cap,
rearming, normal progress, wrap, invalid thresholds and clock ordering. Four
correlation tests cover the prior invariants plus the first triggered frame and
empty triggered capture. The optimized native fixture still recovers 301 known
leaf/middle/entry chains, maximum suspension 0.805 ms. Its progress-only run
causes zero suspensions. Four injected 250 ms stalls produce eight suspensions
(budget two per episode), totaling 3.737 ms, with every context inside a logged
stall interval and every stack containing `oracle_stall`. These fixture numbers
do not establish overhead on the game.

### Triggered jungle capture

Run `20261013-123912-657`, PID 66388, uses ordinary 32-slot candidate SHA256
`37F8F02193F0CACEC97D6AF0D7D2D8BEB8BBEC79756891AF6FA40163286FDB59`,
the matching PDB, a fresh disposable jungle save, muted audio, and the same
1440p/8192/uncapped configuration. After visually clearing the tutorial and
activating Nectar, the sampler watches the owned `ps3recomp_null` window thread
(TID 24960) for 90.021 seconds, randomized 7 ms polls, 50 ms threshold, depth
32, three samples maximum per unchanged counter. Three 24-second audited camera
sweeps with five-second warmups run during the capture; the sampler starts before
these windows and finishes before the final sweep. This entire run is diagnostic,
not an additional ordinary FPS comparison.

There are 11,601 counter observations, 7135 observed changes, three triggered
episodes, and eight retained samples with no overwrite. Total measured
suspension is **6.213 ms**, maximum 1.857 ms; maximum poll gap is 64.557 ms.
The correlator covers 7522 complete pump intervals. All eight samples fall
inside the three longest intervals:

| Pump frame | Elapsed ms | Outer wait ms | FIFO ms | Present ms | Samples | Suspension brackets ms | Observed locations |
|---|---:|---:|---:|---:|---:|---:|---|
| 36669 | 104.920 | 6.325 | 2.853 | 95.736 | 3 | 3.118 | CRT critical-section wait beneath `fprintf`, called by `rsx_live_draw_present+0x24E7` |
| 39703 | 84.442 | 34.554 | 46.698 | 3.113 | 2 | 1.624 | One command-arrival wait; one driver call beneath `ld_flush` |
| 42442 | 80.560 | 27.990 | 50.655 | 1.866 | 3 | 1.554 | Two command-arrival waits; one `CreateFileW` beneath `gcm_vp_burst_poll` |

The first sampled interval precedes the camera windows; the second is in their
intervening warmup; the third is inside a measured sweep. A 50.480 ms pump
interval has no samples, demonstrating the trigger's polling/threshold limit.
These observed stacks do not account for every millisecond of any stall. Driver
frames remain unresolved, and the earlier 400-620 ms stalls still have not recurred.

Source inspection finds unconditional periodic `[live-draw]` output every 32
renderer frames inside presentation; the log contains corresponding renderer
frame 36672. The VP control path opens its configured file every 100 ms while
the FIFO consumer lock is held. These are concrete synchronous diagnostic-I/O
leads. The current user WIP explicitly clears `RSX_VP_BURST_FILE`, so that
particular file-open stack is a benchmark-control lead, not evidence it caused
the user's stalls. The WIP still sets a different CPU-work control file, which
needs a separate audit. Periodic renderer output is shared by both paths.

Camera/mode/present/process checks pass. Resource observations begin **14.380
seconds after the sampler**, so they do not cover the first logging stall; do
not claim full-capture isolation. Seventeen observations bracket the three
camera windows, memory 48.88-49.09%, no compilers observed, only the owned game,
maximum gap 5.321 seconds. No own builds, heavy analysis, or screenshots occur
inside capture. The game exits normally. Private evidence prefix:
`local/jungle-triggered-stacks`, with sampler aggregate (UTF-16), timeline,
correlation, raw pump/presents/camera data and process/resource observers.
