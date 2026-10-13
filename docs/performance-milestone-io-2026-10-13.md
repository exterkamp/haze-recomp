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
