# Producer stalls in automated controller file polling

Tracking: #71, #9 and #32. This follow-up to the 53 ms uninstrumented producer
gap identifies a diagnostic input path that can block the guest main thread.
It is not evidence that normal keyboard/mouse input incurs the same delay.

## Stack evidence

Muted disposable-save run `20261013-143621-116`, PID 36080, uses the existing
`producer-job-trace` candidate with SHA256
`DE57794FA5ED8F53B1EF8FE2B0F8FEE2A7E96EB24F85E7DDED893CA31F727A4B`.
The jungle tutorial was visually confirmed cleared before capture. Settings
remain 1440p/8192 shadows, uncapped, VSync off, fixed CPU flags 13571 and worker
reuse enabled. The native frame-pacer mode log identifies guest main thread
TID 50264 and confirms the original final 30 FPS pacing branch is bypassed.

The existing stack sampler watched that thread for 90.008 seconds, triggering
after 35 ms without observed presentation progress, with a maximum of three
samples per unchanged counter. Camera sweep token 2680 was active. This is a
diagnostic capture with thread suspension, not an FPS comparison.

It retained all 171 samples from 63 trigger episodes, with zero overwrites.
Measured suspension totals 258.149 ms, maximum 4.404 ms. Maximum counter-poll
gap is 70.1343 ms. Samples locate code at those instants; they do not establish
that the thread remained there for every millisecond of a frame.

| Pump frame | Frame ms | Command wait ms | Samples | Overlapping suspension ms |
| --- | ---: | ---: | ---: | ---: |
| 37586 | 3584.796 | 3460.397 | 3 | 4.256 |
| 34023 | 802.708 | 780.084 | 3 | 4.231 |
| 36980 | 708.873 | 690.054 | 3 | 4.209 |

All nine samples in these three longest intervals show the producer inside
`ZwCreateFile -> CreateFileW -> CRT file open -> cellPadGetData+0x65F`, called
through `func_007D7440 -> func_006D1E90 -> func_00680FA0`. Across the entire
capture, 128 of 171 samples include this controller-file call path. These
counts include waits and are not CPU-utilization percentages.

Source inspection confirms that `PAD_FILE` opens the navigation file on every
idle controller poll, even after the driver has finished sending commands.
The interactive WIP launcher clears `PAD_FILE`; its user-facing input path
does not perform this configured file polling. This evidence therefore
invalidates treating the automated input path as cost-free. It does not prove
that PAD_FILE caused the earlier unsampled 53 ms gap or every reported hitch.

Camera and CPU-mode audits pass. The process observer brackets capture but
has a maximum gap of 5.405 seconds, exceeding its standard 4-second gate;
full benchmark validation fails and that gate is not relaxed. Resource
samples show 49.45-51.87% physical memory and no compiler processes, with a
maximum gap of 5.349 seconds. These observations do not exclude activity
between samples. Both capture and game launch finish normally. No builds,
screenshots or offline trace analyses run during the sampling window.

Private evidence: `local/jungle-producer-stacks-{timeline,stacks,correlated}.json`,
pump/job/FIFO/present CSVs, camera/mode report, capture log and process/resource
JSONL. The standard report's failed process-coverage gate is preserved.

## Diagnostic input lifecycle fix

The SDK's existing `PAD_FILE` protocol gains an exact `detach` command, allowing
surrounding whitespace. Once consumed, it clears diagnostic held input and
permanently disables further file polling for that process. It acknowledges
the transition with a flushed `[cellPad] PAD_FILE detached` message on stdout.
Ordinary controller and keyboard/mouse state is unaffected. Existing button
and stick commands retain their behavior until detachment.

`scripts/detach_pad_file.py` sends the command and waits for a fresh native
acknowledgement in the owned run's stdout log. A timeout fails; benchmark
timing must not begin without acknowledgement. Navigation and the visual
gameplay check precede detachment, followed by the normal benchmark warmup.
This changes test setup, not a user-facing graphics or gameplay option.

The native regression fixture extracts the actual SDK command block and
counts file opens. It checks exact parsing, rejects trailing command junk,
preserves normal held-button behavior, and confirms that deleting/recreating
the input file after detachment causes no further opens or input injection.
Existing caller input state is preserved. The test is included in
`scripts/build.ps1 -Test`.

The next acceptance measurement must use an ordinary build, acknowledged
detachment, the same copied jungle save and camera sweep, and no stack
sampling. Only such measurements can establish the remaining frame budget;
the stack-sampled numbers above must not be compared as an FPS speedup.

## First ordinary detached baseline

SDK fix `e6b8a8c` was built without renderer/job profiling, with SPU FMA and
two compiler jobs, into `local/candidates/pad-detach-ordinary/HazeRecomp.exe`.
SHA256: `65AA01AC0D7FC549E9AFF4A018D4B26B47F4EBDF6B2F405A119147AB35652E6B`.
The extracted production-block regression passed with the project's Clang
toolchain. The compile log is `local/pad-detach-ordinary-build.log`.

Muted run `20261013-144826-736`, PID 34800, uses another fresh copy of the same
save and the same graphics settings. The tutorial is visually confirmed
cleared. The detach helper receives its native stdout acknowledgement before
the resource observers and benchmark warmup start. Two 60-second sweeps use
tokens 2700 and 2701, each with five seconds of warmup. Job trace, slow-FIFO
trace and slow-draw trace are disabled. No stack sampling, screenshots, builds
or heavy offline analysis occur in the measured windows.

The ordinary present results across 10,245 intervals are:

| Metric | Result |
| --- | ---: |
| Mean frame time / aggregate FPS | 11.711 ms / 85.39 FPS |
| p95 | 14.854 ms |
| p99 | 17.526 ms |
| Worst | 46.922 ms |
| Over 33.333 ms | 3 |
| Over 50 ms | 0 |

Camera, native-mode and standard process-observer checks all pass. Twenty-five
resource observations bracket timing, memory 47.05-48.54%, no compilers,
maximum resource sample gap 5.342 seconds. The game closes normally with one
exit counter report and no periodic FPS log lines. All seven immutable baseline
save hashes still match.

The matching pump means are 6.460 ms outer command waits, 3.364 ms FIFO work,
1.819 ms present and 0.069 ms other work. These are disjoint pump-thread wall
scopes, not a breakdown of total CPU/GPU work. The worst frame still contains
35.267 ms of outer command waits. Detachment therefore removes a measured
benchmark hazard, but does not establish consistent pacing or explain all
remaining producer delays. The mean frame time is 1.686 times the 6.944 ms
budget for 144 FPS.

This is one clean detached baseline, not a controlled pre/post speedup claim.
The preceding sampled run differs in profiling overhead, sampling and live
world timing. Next, sample remaining producer stalls with input detached and
retain this ordinary run as the timing reference. Private evidence prefix:
`local/jungle-pad-detached-a1`, including the native acknowledgement, camera,
process/resource observations, pump/present CSVs and validated pooled report.
