# Experimental PC CPU kernels

Branch `experiment/native-cpu-kernels` and SDK branch
`experiment/haze-native-kernels` contain this work. The native cursor replacement
defaults off; main retains the established execution path. This is a first
operation migrated to host data and arithmetic, not a replacement of the
entire engine or its memory system.

## Evidence and boundary

The refreshed muted image-40 diagnostic `20261012-163000-781`, ordinary candidate
SHA256 `2651123FB7C7C76DC3C328B751D16BF867BC7DC4D3B24E247BD691FAD61036F0`,
captured the carrier before and after sampling (008882 and 009095). An eight-second
sample observed 168 announced jobs, opened 151 job threads and collected 338 RIPs.
72 locations fall in `3450`, 27 in `35B4`, 38 in task-launch checks and 26 in
call draining. These are sampled locations, including runtime helpers; they are
not CPU-utilization shares or proof of the frame's critical path. Filtered job
logging and brief thread suspension perturb the run, so its FPS is not used as
an ordinary-build comparison. Normal shutdown returned zero.

The original `3450` operation updates a 24-byte cursor: add a float increment at
offset 4 to offset 0, increment index at 8, decrement remaining count at 12, and
add the float increment at 20 to offset 16. The translated leaf repeatedly
loads 128-bit chunks, rotates, shuffles and writes intermediate guest registers.

There are four direct calls, paired in blocks `47E0` and `4900`. The native
replacement is inserted at those paired caller boundaries. It converts each
record into ordinary host fields, performs two float additions and two integer
updates, and writes only the four changed fields. The PC operation itself has
no guest context, registers, LS wrapping or instruction dispatch. The adapter
retains the existing buffer format for callers that have not yet migrated.

The pair preserves ordering even for identical or overlapping records. Both
pointers must be valid, aligned, nonwrapping 24-byte local-store records before
any native write. Image, caller-block and LS ownership guards reject unsupported
contexts; active/uninitialized LS probes keep the original path. The original
leaf and both original call sequences remain as fallbacks. The complete image
hash and the leaf/caller hashes fail closed on changed generated input.

The caller continuations overwrite or discard the leaf's volatile r2..r37
results. The replacement preserves the link, stack, all other registers, branch
decision/target, memory and context metadata at the boundary; it deliberately
does not recreate discarded scratch results. The enclosing routine restores its
saved registers, and its direct outer caller supplies fresh r3/r4 to the next
operation. Broader gameplay validation is still required before default use.

`HAZE_NATIVE_CURSOR=1` enables it. `HAZE_NATIVE_CURSOR_FILE` names a diagnostic
0/1 file for same-process comparisons; mode changes are logged with QPC and a
first accepted-operation message verifies that the guards accepted actual work.
Only the diagnostic selector uses synchronization; it does not fabricate guest
job completion or alter DMA/queue signals.

## Correctness and CPU-cycle measurements

The oracle extracts the actual original leaf and both caller blocks locally.
4,200 comparisons cover both caller branches, all four word alignments, identical
and overlapping records, random float bit patterns, integer overflow and original
fallbacks for unaligned/out-of-range/wrapping pointers. Accepted cases compare all
memory, control transfer, context metadata and registers outside the audited
volatile set. Fallback cases compare the full context byte-for-byte. Guard mutation
checks require unchanged state. This is a boundary-contract comparison, not an
assertion that the deliberately discarded scratch registers remain identical.

Four one-million-pair CPU-cycle runs measured original 251.642..254.514 cycles
versus native 47.768..50.541 cycles (4.988..5.293x). The fixture's call-drain and
image-restore stubs check the leaf's simple return contract but omit real runtime
cost. These figures describe the extracted pair, not whole-game speed or an FPS
multiplier. The full candidate build and regression suite passed.

The SDK additionally removes four byte-order shuffles from the general SIMD
shuffle helper. It operates directly in host order and maps source selectors
through XOR 3, preserving selector aliases and 00/FF/80 special results. Tests
cover every selector at every output byte (4,096 cases), 200,000 random controls,
and the existing numeric/byte/LS suites. A standalone retained-legacy comparison
measured about 20.2..20.5 versus 16.2..16.6 cycles per dependent operation across
SSE and AVX/FMA builds. This is a local CPU saving, not a measured FPS gain.

Artifacts: `local/image40-refresh2-{run,samples}.json`,
`local/native-cursor-{oracle,build-tests}.log`, `local/cursor_oracle.c` (private
game-derived fixture), and `local/shufb-direct-{sse,avx}-cost.log`.

## Ordinary candidate gameplay validation

Candidate `local/candidates/native-cursor/HazeRecomp.exe`, SHA256
`55BB364CA52CD4D93D5ECEFCAE2FCDB5DD2A662A3EC84118DFF02D7670005EAF`,
passed the full build/regression suite. Muted run `20261012-165219-051` used
2560x1440 rendering, VSync on, host FPS limit zero, follow shadows, AA off and
AF 1. Native PPU queue and SPU idle waits were on; native triplet copying was
off. The build had no CPU/renderer/job profiling enabled.

Four 30-second windows with five-second warmups switched only the cursor mode.
There was no input, frame capture or sampling inside the measurement windows.
Mode events validate each window, and the accepted-operation event confirms
execution through the native replacement in the carrier. Captures 008372 and
013262 show the carrier before and after; normal shutdown returned zero.

| Cursor mode | FPS | Mean frame ms | p95 ms | p99 ms | Process mean cores |
| --- | ---: | ---: | ---: | ---: | ---: |
| Original | 20.751 | 48.191 | 65.090 | 89.788 | 3.918 |
| Native | 20.323 | 49.206 | 65.013 | 77.112 | 3.947 |
| Native | 21.532 | 46.443 | 62.141 | 79.425 | 3.932 |
| Original | 20.371 | 49.090 | 65.086 | 78.793 | 4.035 |

Another agent's Haze process (PID 31556, shadow-lighting worktree) started after
launch and was observed during measurement. The strict isolation check rejected
the run. The table is a contended diagnostic, not evidence of an FPS or pacing
improvement. The other process was left untouched. The kernel remains off by
default pending a repeatable FPS benefit and broader gameplay validation.

Artifacts: `local/native-cursor-abba-{run,windows}.json`,
`local/native-cursor-abba-presents.csv`,
`local/native-cursor-abba-processes.jsonl`, and
`local/native-cursor-abba-contended-report.json`. The latter deliberately omits
the strict observer check to expose the raw diagnostic numbers above; it must
not be presented as an isolated result.

## Rejected lead: floating-point denormal handling

The translated cursor's discarded SIMD lanes reinterpret small integer counters
as subnormal floats. To test whether those lanes explain its cost, the extracted
fixture explicitly cleared, then set, both FTZ/DAZ bits for four repetitions per
mode. Original cost was 230.8..233.2 cycles with the bits clear and
228.6..239.7 with them set; native cost was 31.9..43.9 across both modes. This
does not support denormal handling as the explanation for the original pair's
cost. The diagnostic restores MXCSR afterwards and never changes game FP mode.
Correctness checks precede these normal-float timing fixtures. The repeat also
illustrates variation in the microbenchmark's native cost; the speedup is local
to this operation, not a fixed whole-game multiplier.

Artifact: `local/native-cursor-denormal-cost-verified.log`.

## In-game job CPU-cycle comparison

The user authorized continuing comparisons alongside the graphics agent's game;
an isolated launch is desirable for attribution, not a prerequisite for work.
Use repeated mode windows and label contention instead of treating it as a
blocker. Thread CPU counters provide another view alongside wall-time/FPS.

Muted run `20261012-171450-040` used the same ordinary candidate/settings above,
with `SPU_JOB_TIMING=1` and `SPU_JOB_TIMING_IMAGE=40`. This adds diagnostic logging
and per-job `QueryThreadCycleTime` counters; its FPS is not an ordinary-build
performance result. Captures 020563 and 024961 verify the stationary carrier
view before/after. No input or instruction sampling occurred inside the four
30-second windows, each preceded by five seconds of settling. The owned process
closed normally with exit zero. Other Haze launches were observed concurrently.

`summarize_spu_job_modes.py` requires complete windows and matching settled
native-mode events. It counts only selected-image jobs whose setup and execution
both fit entirely inside a window. Tests reject missing/late/wrong/changed modes,
incomplete or invalid counters, insufficient jobs and incomplete windows; jobs
straddling boundaries and other images are excluded from fixture totals.

| Cursor mode | Complete jobs | Mean run cycles, millions | Median cycles, millions | p95 cycles, millions | Mean job wall ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| Original | 463 | 66.231 | 66.553 | 70.558 | 16.299 |
| Native | 469 | 57.114 | 57.271 | 60.574 | 13.844 |
| Native | 447 | 57.324 | 57.275 | 60.877 | 14.408 |
| Original | 492 | 63.367 | 64.889 | 70.280 | 15.223 |

Native mode reduces this completed job's measured CPU cost by roughly 10–14%
against the bracketing original blocks. Cycles exclude descheduled time but
still depend on CPU frequency/core placement and workload changes; they are not
a fixed conversion to milliseconds. The ABI/native inner phase counters are
zero in this ordinary candidate and are not interpreted as zero work; only the
outer run/setup counters are available. Setup is about 0.08..0.10 million cycles
per job. Instrumented, contended FPS was 15.435 / 15.660 / 14.916 / 16.414: no
consistent frame-rate improvement follows from the job saving.

A separate native-enabled image-40 sample after all timed windows collected 200
RIPs from 162 announced jobs (134 opened, 28 already gone). The original `3450`
leaf has no samples; native cursor helpers appear instead. The remaining leading
locations include task-launch checks (36), the `35B4` scan loop (22), drain (15),
lockstep tick (9) and `35D0` scan continuation (8). Counts include helpers/waits
and are not CPU shares. This supports a shift away from the cursor leaf without
establishing which remaining work delays presentation. Investigate main-thread
waits/frame submission next before assuming a second SPU kernel raises FPS.

Artifacts: `local/native-cursor-job-{run,windows,cycle-report}.json`,
`local/native-cursor-job-presents.csv`, `local/native-cursor-job-processes.jsonl`,
`local/native-cursor-job-instrumented-present-report.json`, and
`local/native-cursor-job-enabled-samples.json`.

An earlier attempt `20261012-170457-044` accidentally selected a new chapter
instead of Continue and spent much of its allowance in the intro. Its ten-second
CPU sample is retained as `local/native-cursor-intro-cpu.json`; snapshots missed
12,078 of 31,500 process CPU ms. Its timeout prevented normal CSV flushing.
It supplies no FPS comparison. The subsequent run loaded Continue explicitly
and scheduled normal close directly after measurements.

Consistent frame pacing and broad unlocked-rate gameplay validation remain
unfinished; the full goal stays active.

## Frame submission and main-thread waits

Two separate muted, normally closed carrier runs used the profile candidate
`local/candidates/native-cursor-frame-profile/HazeRecomp.exe`, SHA256
`DB7F935450FF7E62F72EE7AD02C65494AFD76C2F15E13B94B61F87357B326E45`.
Both retained 1440p, VSync on, FPS limit zero, native cursor/PPU queue/SPU idle
enabled, triplet copying disabled. Wait/queue counters and image-42 job timing
were recorded during a 20-second stationary window. Instruction sampling ran
afterwards, outside that window. Other Haze processes overlapped these runs.
Captures 008166/009859 and 010175/012251 verify the carrier view before/after
the respective runs. These are diagnostic timings, not ordinary-build FPS tests.

The first run, `20261012-172645-268`, exposed instrumentation overhead. Its
pipeline preparation averaged 16.51 ms, with zero shader compilations or PSO
creations. Separate instruction samples landed repeatedly in
`ld_profile_note_hash` and its hash inputs. The profile-only shader-variant table
scans its entire capacity when a new hash cannot be inserted. That work does
not exist in ordinary builds and must not be called a production bottleneck.

The second run, `20261012-173044-399`, enabled existing `RSX_PROFILE_LIGHT=1`,
which disables that optional variant tracking while retaining renderer timings.
For 338 frames with at least 500 draws (about 1,396 draws/frame), it measured:

| Light-profile measurement | Mean ms |
| --- | ---: |
| Present interval | 59.04 |
| Before first draw | 16.82 |
| First-to-last draw span | 40.82 |
| Time in renderer draw processing | 30.22 |
| Vertex packing | 9.36 |
| Texture hashing | 4.11 |
| Texture decoding | 2.15 |
| Pipeline preparation | 4.72 |
| Vertex/fragment program preparation | 2.64 / 1.62 |
| GPU timestamp duration | 2.22 |
| Fence wait | 2.67 |

Renderer subcategories overlap their parent scopes; do not add them to frame
time. CPU-side timers measure elapsed scope duration and can include preemption.
The GPU timestamp does not represent all work or prove that contention is absent.
The 16.94 diagnostic FPS and first run's 19.69 FPS are not a controlled heavy/light
comparison: scene workload and concurrent launches changed. The useful result is
removal of the misleading profile-only hotspot and identification of substantial
draw processing and variable delay before the first draw. Image-42 jobs averaged
5.65 ms wall time (p95 10.13 ms, 850 completed jobs); correlation alone does not
establish that they cause those delays.

The main guest thread (guest TID 1) also spent at least 3.13 seconds of the
20-second light window in 330-us sleeps at LR `0068EFF0`, called through
`00682574`. Individual sleeps reached 6.50 ms in one caller group. These waits
overlap other threads' work and cannot simply be subtracted from frame time.
This is separate from the already bypassed final frame-pacing wait.

Inspection identifies the loop as `00682538`: it checks `0071F6F0` for an indexed
work record, sleeps if incomplete, and services pending work before checking
again. The record table uses a 0x1450-byte stride; `006820B0` reads its status
byte at offset 8. One completion path also checks offset 9 and drains a queue
through `0071F648`. Any native wait replacement must preserve both real
completion and this work servicing, including publication/wake ordering.
Removing the sleep without identifying the producers is not justified.

Separate eight-second light-profile CPU samples measured roughly 4.72 CPU
seconds on the main thread, 3.94 on the renderer host, and 4.75 on the audio
thread. These are another interval, not per-frame costs. The queue counters
showed much more time waiting for work than acquiring mutexes, with no queue errors. The
next investigation should distinguish producer completion delays from native
render preparation before choosing another kernel to replace.

Artifacts: `local/native-frame-{critical,light}-{run,window,report,cpu}.json`,
their renderer/present CSVs, wait directories and queue directories. The profile
candidate build and configuration checks passed; the ordinary candidate's full
regression suite had already passed before these diagnostic-only additions.

## Shader metadata cache with the native cursor enabled

An ordinary-build repeat tests whether the earlier VP metadata cache becomes
useful for FPS with the image-40 kernel enabled. Native cursor mode remains one
throughout; only CPU-work bit 512 changes. Flags 1283 retain the default native
vertex/index paths; 1795 additionally enables the byte-validated VP cache.
This caches program analysis, not vertex data or game state.

Muted run `20261012-174151-688` used candidate `55BB364C...` and the same 1440p,
uncapped/VSync-on settings. A fresh private copy of the checkpoint was loaded
through Continue. Captures 012876 and 016264 verify the carrier before/after;
normal close returned zero. Four 30-second windows followed five-second warmups,
with no input, captures, builds or instruction sampling inside them. The existing
present reporter verified complete consecutive intervals and settled CPU-work
flags. The process observer saw only the owned Haze in every block, with maximum
observation gaps of 2.14..2.19 seconds; this does not exclude other applications
or processes whose entire lifetimes fall between observations.

| VP cache | FPS | Mean frame ms | p95 ms | p99 ms | Process mean cores |
| --- | ---: | ---: | ---: | ---: | ---: |
| Off | 19.092 | 52.379 | 78.808 | 95.129 | 3.689 |
| On | 20.277 | 49.317 | 71.522 | 92.263 | 3.711 |
| On | 22.347 | 44.749 | 64.209 | 85.164 | 3.885 |
| Off | 21.073 | 47.454 | 66.215 | 89.890 | 3.702 |

The combined result is encouraging but performance also improves over time
within each mode. Animated NPCs/vehicles keep changing the workload despite a
stationary camera. This single ABBA sequence does not establish a repeatable
cache benefit. The reversed-order repeat below did not confirm the improvement.
Neither stable frame pacing nor the desired framerate is achieved.

Artifacts: `local/native-cursor-vpcache-{run,windows,report}.json`, its presents
CSV and process-observation JSONL. The native accepted-operation event appears
before measurement and the present-ring CSV covers all four blocks.

The reversed sequence used fresh fixture/run `20261012-174731-532` with the same
ordinary binary and settings. Captures 010261/013797 verify gameplay; normal
close returned zero. Another agent's Haze (PID 57144, visibility-geometry-trace
candidate in the shadow-lighting worktree) overlapped every measured block.
Work continued without modifying that process. The strict reporter rejected
isolated attribution; the separate contended report retains the validated raw
present intervals without invoking its isolation assertion.

| VP cache, reversed order | FPS | Mean frame ms | p95 ms | p99 ms | Process mean cores |
| --- | ---: | ---: | ---: | ---: | ---: |
| On | 17.765 | 56.289 | 83.419 | 103.618 | 3.511 |
| Off | 21.540 | 46.424 | 62.456 | 69.392 | 4.181 |
| Off | 18.989 | 52.662 | 78.024 | 92.197 | 3.999 |
| On | 19.297 | 51.821 | 69.972 | 80.000 | 3.900 |

The repeat is mixed and does not establish a consistent FPS or pacing benefit.
It also cannot quantify the effect of the other process. Keep the cache opt-in;
do not convert either sequence or the isolated kernel microbenchmark into an
end-to-end speedup claim. The confirmed image-40 job cycle saving remains a
separate result. Next work needs better attribution of the remaining main-thread
memory-helper and rendering preparation costs, rather than another default
switch based on noisy FPS alone.

Reverse artifacts: `local/native-cursor-vpcache-reverse-{run,windows}.json`,
`local/native-cursor-vpcache-reverse-contended-report.json`, its presents CSV,
and process-observation JSONL.
