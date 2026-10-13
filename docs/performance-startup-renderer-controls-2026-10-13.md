# Fixed startup controls

Tracking #71, #9 and #32. The previous deferred-log experiment still has long
FIFO and command-arrival stalls. A prior triggered stack also found a VP
control-file open on the FIFO consumer while its lock was held. The ordinary
WIP clears that VP override but still polls a fixed CPU-preparation file.

SDK `11e1e1a` adds a title-owned startup setter for CPU preparation flags;
the generic SDK default stays 1283. Haze selects 1283 | 8192 before allocating
guest memory. Successful texture write-watch registration adds 4096, giving
the existing WIP's 13571. Registration failure leaves tracking disabled and
its existing fallback intact. The existing optional `RSX_CPU_WORK_FILE` live
override remains available; normal launches no longer need it to select
Haze's native index path. No new graphics setting or environment opt-in.

The ordinary profiling-off/FMA build succeeds with two jobs:
`local/candidates/startup-renderer-controls/HazeRecomp.exe`, SHA256
`8F1FE5AF9EC175669F4257B9DCAF4AC393A288FD7E39BF86FA740D82A0643A7A`.
The benchmark recorder gains `--fixed-mode`: no setting file is written,
all requested flags must agree, and native mode/CSV verification still
applies. Existing three present-report tests and camera-trajectory test pass.

## Comparison protocol

Compare fixed/polled/polled/fixed launches of this same binary. Both select
CPU flags 13571, VP burst 1 and worker reuse 1. Fixed launches clear
`RSX_CPU_WORK_FILE`, `RSX_VP_BURST_FILE` and `SPU_WORKER_REUSE_FILE`; polled
launches retain files holding those same values. All windows use the fixed
recorder with CPU flags verified from every present sample. The files under
comparison remain constant; PAD and camera files still carry changing
automation input. Startup logs must additionally confirm VP/reuse modes.

Each launch gets a fresh copy of the immutable real jungle save, 1440p,
8192 shadows, VSync off/uncapped, muted audio and the same native paths.
Visually verify gameplay after the Nectar tutorial before timing. Record
two 24-second absolute 30-degree camera sweeps after five-second warmups.
No builds, stack sampling, screenshots or heavy analysis during timing.
Preserve all intervals and record resource/process coverage. Live NPC/world
state and navigation timing are limitations. These tests do not replace
attribution of the remaining long command waits or prove stable 144 FPS.

## Completed comparison

| Run | Fixed-setting files | Intervals | Mean ms / FPS | p99 ms | Maximum ms | Over 50 ms |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| A1 `20261013-133841-595` | Cleared | 4114 | 11.660 / 85.76 | 18.790 | 43.469 | 0 |
| B1 `20261013-134242-481` | Polled | 4129 | 11.622 / 86.04 | 17.075 | 40.469 | 0 |
| B2 `20261013-134634-432` | Polled | 4144 | 11.578 / 86.37 | 16.752 | 48.000 | 0 |
| A2 `20261013-135050-330` | Cleared | 3342 | 14.356 / 69.66 | 26.701 | 65.423 | 2 |

All windows pass native CPU-flag, VP-mode, reuse-mode, actual-camera and
owned-process audits. Each run emits one balanced exit counter report and no
periodic FPS lines, and closes normally. The seven immutable save files retain
their original hashes. No compiler processes are observed in any window;
physical memory ranges from 48.72% to 51.07%, maximum resource gaps
5.33-5.36 seconds. No screenshots, stack sampling or builds overlap timing.

Descriptively pooled fixed runs are 12.868 ms / 77.71 FPS, p99 22.400 ms;
polled runs are 11.600 ms / 86.21 FPS, p99 16.882 ms. This experiment does not
establish a speedup. The first fixed/control pair is nearly identical, while
the final fixed repeat is substantially slower. All intervals remain in the
result. Live NPC activity and timing after navigation differ between launches;
there is no evidence attributing the final slowdown to polling removal or to
compiler/memory pressure. Neither configuration reproduces the previous
hundreds-of-milliseconds hitches during this comparison.

Evidence: `local/startup-controls-comparison.json`, the four
`local/jungle-{fixed,polled}-controls-*` CSV/JSON captures and process/resource
logs. The fixed-mode recorder is exercised in all four launches, validating
actual settings without mutating control files. This startup change eliminates
the need for a fixed CPU-control file and enables cleaner experiments, but is
not accepted as a performance gain. The existing WIP launcher/binary is
unchanged. Continue direct attribution of slow drains and delayed command
arrival rather than inferring a fix from a single clean run.
