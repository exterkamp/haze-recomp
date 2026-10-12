# Native wait for Haze's persistent SPU service

Consistent frame pacing and correct unlocked gameplay remain unverified. This
experiment removes measured empty-queue polling from one persistent worker;
it does not skip simulation work. Both this wait and the earlier native PPU
queue wait remain opt-in.

## Measured target and patch

Ordinary-build CPU sampling of the combined queue/shadow-clear candidate in
run `20261012-073524-653` attributed 4,890.625 ms of CPU over 6.004 seconds to
host TID 66556. Samples repeatedly landed in compiled functions `0x3B70` and
`0x3C7C` of lift `spu_0028_at_00EC7980`. Another Haze process was running, so
this identifies a hot worker rather than establishing an FPS baseline.

The directory number is zero based: this lift registers as **runtime image
29**, as verified in `generated/spu/spu_workloads.c` and the live context.
Earlier notes calling this the image-28 service referred to its filename.

The audited caller at LS `0x3378` retries the same queue pop immediately on
failure. A build-local copy adds a host wait at that retry. The second retry,
after a different queue-size operation, is unchanged. The original translated
pop still runs before and after every wait and retains its register, local
store, atomic reservation and consumption behavior.

Parking requires runtime image 29, return PC `0x33A4`, the actual AGAIN code,
the audited caller registers and descriptor layout, no enabled pending SPU
interrupt, and a genuinely empty, unclaimed PPU-to-SPU queue. Queue inspection
and watch registration use the existing atomic-line lock. `WaitOnAddress`
compares the raw eight-byte header snapshot, so publication between unlocking
and sleeping returns immediately. Coherent publications wake the thread;
a one-millisecond timeout bounds missing notifications. Hash collisions
decline parking. Every wait removes its reference-counted watch before return.
The helper writes no guest registers, local store, queue indices or payloads.

`HAZE_SPU_IDLE_WAIT=1` enables the experiment. `HAZE_SPU_IDLE_CONTROL` instead
allows a live 0/1 diagnostic file, checked at most once per 100 ms per worker.
Native mode events record the actual host TID and QPC timestamp. The patcher
fingerprints the complete source and caller and verifies runtime registration;
an unexpected lift fails the build for re-audit. Shared generated sources stay
unchanged and ignored.

## Correctness gate

The ordinary FMA build passed `scripts/build.ps1 -SpuFma -Test`, exit 0,
recorded in `local/spu-idle-wait-verified-build-tests.log`. Its executable is
`local/candidates/spu-idle-wait-verified/HazeRecomp.exe`, SHA-256
`FE915652B8302B4A2BE81EC8E3F31A1E66D025DAF176BB4EDD68C02F40737E95`.
Profiling and live scalar/SIMD comparisons are compiled out.

The native regression checks wrong image/PC/error/descriptor, enabled pending
interrupts, ready/claimed/malformed queues, watch collisions, real publication
between unlock and wait, bounded empty-queue fallback, complete architectural
state preservation and notifications after protecting the freed mapping.
Existing queue tests also cover ordered real delivery, wraps and blocked wakes.
The present reporter requires this worker's own settled mode events; a PPU
queue event with the same TID is insufficient evidence.

## Worker CPU diagnostic

Muted run `20261012-080717-627`, owned PID 61524 / worker TID 65084, recorded
four eight-second title/menu windows using `GetThreadTimes` and
`QueryThreadCycleTime`, without thread suspension. Mode events settled before
each window, and the real queue park was confirmed. Another Haze instance was
running; one screenshot occurred during the first native window. These are
worker CPU measurements, not an isolated gameplay/FPS comparison.

| Mode, in order | CPU ms | Mean CPU cores | Thread cycles |
| --- | ---: | ---: | ---: |
| Poll | 7031.250 | 0.879 | 32,896,890,480 |
| Native | 171.875 | 0.021 | 340,922,490 |
| Native | 46.875 | 0.006 | 339,667,590 |
| Poll | 7171.875 | 0.896 | 34,188,240,390 |

The approximately 99% reduction in this worker's cycles does not imply the
same reduction in whole-process CPU or frame times. Raw evidence is
`local/spu-idle-worker-cpu.json`. The run closed normally with exit 0.

That long menu session ended with a blank profile and no profile autoload
calls. Fresh copies of the same saves loaded successfully with the same
binary both disabled from startup (`20261012-082132-805`) and enabled from
startup (`20261012-083304-158`). The earlier result is unexplained and must
not be represented as either a confirmed regression or a resolved defect.

## Ordinary-build gameplay comparison

Run `20261012-082132-805` loaded an isolated copy of the known carrier saves,
with SPU idle waits initially disabled and native PPU queue waits fixed on.
Resolution was 2560x1440, shadows followed resolution, AA was off, VSync auto,
and the presentation ceiling was 30. Captures checked the stationary opening
carrier before and after measurement. Four 60-second blocks followed
five-second warmups: poll/native/native/poll. No sampling, readbacks, job
logging, builds or camera input occurred inside these windows.

| Mode | Frames | FPS | Mean ms | p95 ms | p99 ms | Max ms | Mean CPU cores |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Poll | 1017 | 16.98 | 58.88 | 84.66 | 104.29 | 179.94 | 4.73 |
| Native | 1335 | 22.27 | 44.90 | 66.69 | 85.38 | 119.97 | 4.38 |
| Native | 1354 | 22.59 | 44.26 | 64.28 | 81.32 | 129.20 | 4.27 |
| Poll | 1117 | 18.64 | 53.63 | 74.71 | 86.36 | 106.97 | 5.18 |

The strict present reporter passed with actual worker TID 60204. It retains
all consecutive present intervals and stalls within each window. The
two-second process observer recorded only the owned game throughout the
measured windows. The game closed normally with exit 0 at 08:32:17 UTC.
Artifacts are `local/spu-idle-abba-{windows,report}.json`,
`local/spu-idle-abba-processes.jsonl`, and the run's native logs/presents CSV.
NPC activity varies, and the two polling windows differ. This first comparison
supports a gain but still falls short of consistent 33.3 ms frames. It does
not verify unlocked simulation timing or display scanout cadence.

After these windows, a separate six-second native-mode CPU sample recorded
22,421.875 ms process CPU but attributed only 12,046.875 ms to observed
threads. The 10,375 ms coverage gap includes short-lived jobs missed by thread
snapshots. RIP counts include waiting threads and are not CPU percentages.
The next CPU investigation must address that gap instead of assuming all
remaining time belongs to rendering or thread creation. Evidence is
`local/spu-idle-native-game-cpu.json`.

The independent reverse-order preparation `20261012-083304-158` verified
profile autoload, checkpoint load and the carrier with native idle waits
enabled from startup (worker TID 36080). Another agent's Haze PID 52348
appeared inside its first 60-second timing window. The recorder was stopped
and its `local/spu-idle-baab-windows.json` remains incomplete; the strict
reporter rejects it. `local/spu-idle-baab-processes.jsonl` preserves the
contention evidence. The owned game exited normally with code 0 at
08:40:00 UTC. This run supplies startup/gameplay correctness evidence but
no isolated reverse-order FPS result. Reversed-order validation and the
slow squad-conversation test remain outstanding before default promotion.
