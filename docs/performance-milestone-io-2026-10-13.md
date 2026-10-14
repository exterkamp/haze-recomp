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

## Live asynchronous-output validation

Haze `31bab7a` / SDK `c64652d` builds successfully with two jobs. Candidate
`texture-inner-async` SHA-256 is
`193D3E9B8C9E9B83F3DC303C0D588A66FAC248188A4D7B1FCBE726B202A341D0`.
Muted run `20261013-235526-294`, PID 55384, uses the same private jungle fixture
and fixed 1440p controls, with milestones explicitly enabled and detailed
renderer recorders disabled. The settled scene is checked and PAD_FILE detached.
The game exits normally; the 812,308-byte milestone log contains 152 contiguous
first-key ordinals, 161 count checkpoints, no overflow marker and exactly one
final `# end`. Original save hashes still match.

A 90.012-second producer capture (TID 56804, camera token 3580) triggers after
25 ms without observed present progress. All three samples are retained across
two episodes. The maximum counter-poll gap is 84.289 ms, so the capture misses
some hitches; this is not complete attribution. One sample is in
`WaitOnAddress -> gcm_fifo_wait_for_publication -> gcm_fifo_wait_for_ea ->
cellGcm_fifo_recycle`. Two subsequent samples lie inside pump frame 48138:

- `vm_read32+0x82 -> func_0051A6C0 -> func_002254A0 -> ... -> func_00680FA0`
- `vm_read32+0x1 -> func_004B90D0 -> ... -> func_005FBB70 -> func_00680FA0`

Frame 48138 is 40.1796 ms: 31.8087 ms command waits, 6.2776 ms FIFO and
1.9412 ms presentation. Its two suspension brackets total 3.9383 ms. The
43.2246-ms maximum frame is unsampled. No sampled producer stack contains
milestone file I/O, but three observations cannot prove all hitches gone or
assign CPU shares. These stacks motivate inspecting command-buffer handoff
and the identified game-code paths.

All 7,320 perturbed intervals are retained: 81.331 FPS / 12.295 ms mean,
15.829 ms p95 / 18.830 ms p99 / 43.224 ms max, three above 33.333 ms and none
above 50 ms. This is not an FPS comparison or release acceptance. Another
Haze PID 51756 overlaps, so isolation fails; no compiler overlaps. Nineteen
resource samples have a maximum 5.368-second gap, memory 53.46-54.18%.
Evidence prefix: `local/jungle-milestone-async-live-a1`, including correlated
stacks and failed isolation reports.

Source follow-up on the two memory-read samples: `func_0051A6C0` is a
bounds-checked float-array accessor, called twice by `func_002254A0`, which
contains a dynamic loop, FP accumulation and several helper calls.
`func_004B90D0` follows object pointers to a dynamic CTR/TOC target, invokes it,
and copies three resulting floats. Neither sampled leaf establishes a dominant
algorithm or safe dynamic-target identity for a native replacement. Prior
broad inline-read comparisons are already inconclusive (see
`performance-native-read-profile-2026-10-13.md`); do not repeat that experiment
based only on these snapshots. Call duration/count or resolved-target evidence
would be needed before selecting either routine for replacement.
