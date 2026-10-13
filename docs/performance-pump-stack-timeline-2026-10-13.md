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
