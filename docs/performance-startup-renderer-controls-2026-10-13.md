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
