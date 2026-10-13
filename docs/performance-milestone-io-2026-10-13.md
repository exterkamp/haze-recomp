# Milestone logging blocks the command producer

Tracking: #71 and #32. This identifies and removes a performance-measurement
confounder; it is not evidence that normal interactive gameplay became faster.

## Triggered stack evidence

Run `20261013-232437-243`, PID 65256, uses the ordinary resource-phase candidate
SHA-256 `C1CF212D382D3A1873DDCFD99E3C742A5F83B346E78807ABAAE74DC4BDA76766`.
Detailed draw/FIFO/flush recorders are disabled. The game is muted and resumes a
private copy of the jungle checkpoint at 1440p, 8192 shadows, uncapped/VSync off.
The injection animation is finished and the scene visually checked before
PAD_FILE detach and camera token 3520. The producer TID 47476 comes from the
native frame-pacer startup identity, not thread age or a guessed window thread.

The existing sampler watches present progress for 90.006 seconds, triggering
only after 40 ms without observed counter advancement. It retains three stack
samples across one episode, without overwrite. Maximum poll gap is 115.473 ms,
so the trigger can miss stalls. This is stack attribution, not an FPS benchmark.

All three samples fall within pump frame 44531:

| Scope | Elapsed ms |
| --- | ---: |
| Complete pump interval | 231.9066 |
| Command kick waits (197 calls) | 223.5101 |
| FIFO processing | 6.1062 |
| Presentation | 1.9468 |
| Other/tick | 0.3435 |
| Overlapping sampler suspension brackets | 4.9647 |

Each stack shows `ZwWriteFile -> WriteFile -> fflush -> ms_checkpoint ->
ps3_ms -> ps3_msf -> ps3_hle_call -> func_0071F648`, beneath Haze's job-completion
service. This establishes that the producer is blocked in milestone output at
those observations. It does not assign every millisecond of the frame to that
operation or explain the separate unsampled 52.212-ms interval.

`runtime/milestone.c` synchronously appends and flushes count checkpoints under
its global spinlock. Calls periodically enter this path from hot HLE dispatch.
`scripts/run.ps1` unconditionally sets `PS3_MILESTONE_OUT`; the interactive
launcher does not normally enable it. Earlier benchmark tails can therefore
include diagnostic log stalls absent from the normal interactive setup.

For completeness, the sampled window contains all 7,622 intervals: 84.687 FPS,
11.808 ms mean, 15.223 ms p95, 18.113 ms p99, 231.907 ms maximum, six above
33.333 ms and two above 50 ms. These are perturbed observations. Another Haze
PID 52552 and up to one compiler overlap; strict isolation fails. Eighteen
resource samples bracket the window, maximum gap 5.352 seconds, memory
54.07-55.31%. No owned compile/screenshot/heavy analysis overlaps capture.
The game and capture close normally; immutable real-save hashes match.

Evidence prefix: `local/jungle-producer-stall-latest-a1`, including raw timeline,
stacks, pump/present/camera records, correlated intervals, resource observations
and validation reports. No generated game code, saves or assets are committed.

## Runner correction

`scripts/run.ps1` now clears milestone logging by default, including an inherited
`PS3_MILESTONE_OUT`. Explicit `-Milestones` restores the boot diagnostic output;
`boot-result.json` records `milestone_logging`. Environment restoration after
the child exits remains unchanged. PowerShell syntax validation passes.

A new muted same-binary baseline validates this correction separately from the
stack capture. The SDK's logging implementation also needs a bounded background
writer so enabling diagnostics cannot put filesystem I/O on frame-critical
threads. Those follow-ups do not make consistent pacing or 144 FPS complete.

## Logging-disabled validation

Run `20261013-233403-144` uses the same binary and two 45-second settled jungle
camera sweeps, tokens 3540/3541. An intentionally inherited milestone output
path and the run's boot-milestones.log both remain absent; boot-result.json
records `milestone_logging: false`. The muted game exits normally and original
save hashes match.

All 6,129 intervals are retained: **68.119 FPS / 14.680 ms mean**, 19.669 ms p95,
25.274 ms p99, 50.857 ms maximum; 20 above 33.333 ms and one above 50 ms.
Individual windows average 71.423 and 64.815 FPS. Disjoint pump means are
8.009 ms wait, 4.570 ms FIFO processing, 1.961 ms presentation and 0.140 ms
other/tick. Waiting does not establish removable idle time.

Another Haze PID 15056 and up to three compiler processes overlap, so strict
isolation fails. Twenty resource observations bracket the windows, maximum
gap 5.380 seconds, memory 54.23-55.11%. No owned build, screenshot or heavy
analysis overlaps timing. These results verify the unwanted logging path is
off, but establish neither an FPS gain nor a regression against previous runs.
Evidence prefix: `local/jungle-milestones-off-a1`.

## Background writer implementation

The SDK now queues first-key and scalar records in a bounded 4,096-entry queue.
A background writer owns file open, formatting, flush and close; repeat keys
only update atomic counts. Overflow is explicit in the log. The writer checks
its two-second checkpoint deadline between bounded drain batches, including
under continuous traffic and when guest calls stop. I/O failure disables later
records without spinning; shutdown still joins the writer and drains accepted
records. The unset-environment path creates no writer or output file.

The five actual-source regression cases cover disabled output, a parked writer
with responsive producers and explicit overflow, checkpoint persistence without
clean exit or further guest calls, continuous-traffic checkpoints, and failed
open with low idle CPU and clean shutdown. The existing SDK milestone suite
also passes. The blocked-writer case includes concurrent/idempotent dump calls.
These are implementation checks; a full game build and live check are separate.
