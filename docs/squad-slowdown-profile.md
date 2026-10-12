# Carrier and squad slowdown profiling

The user reported a sustained 1–2 FPS drop during the first squad conversation
at 2560x1440. Their session `20261012-053859-731` records repeated 1.6–2.1 FPS
windows over roughly 50 seconds. They did not deliberately wait to determine
whether it eventually recovers. Its settings used `shadow_resolution=follow`,
which creates a 5120-square atlas at this rendering resolution. The texture
cache was already full while the game was running at about 25 FPS, so cache
occupancy alone does not explain the sudden change.

The low-rate interval starts around frame 4512, with null reads in guest
function `00AD8D00`, followed by reads in stack-walking function `00722E98`.
These are leads requiring reproduction, not demonstrated causes.

## Muted instrumented observations

Both runs below used main `fb4980a` / SDK `45faa42`, SPU FMA and profiling
enabled, explicit 1440p settings matching the reported shadow scaling, AA off,
filtering 1, cap 30 and a 1280x720 window. They closed normally with exit 0.
These are diagnostic observations, not controlled optimization comparisons.

Run `20261012-054526-202` reached the squad and briefly fell to 5.7–5.8 FPS,
then recovered through 12 FPS to about 26–27. It did not reproduce the user's
sustained 1–2 FPS drop or the same null reads.

| Interval | Frames | Mean frame ms | Draw CPU ms | GPU ms | PS compile ms | PSO creation ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Before slowdown | 11811–12211 | 37.44 | 15.57 | 1.14 | 0 | 0.029 |
| Slow window | 12328–12387 | 173.05 | 22.42 | 1.61 | 0.400 | 0.531 |
| Recovery transition | 12388–12447 | 83.32 | 24.87 | 2.09 | 0.586 | 0.057 |
| Recovered | 12826–13222 | 37.92 | 20.01 | 1.94 | 0 | 0.00025 |

During slow two-second windows, the renderer pump spent 1641–1681 ms waiting,
287–321 ms draining FIFO work, and 35–38 ms presenting, with 11–12 presents.
The frame limiter was inactive. This points toward delayed guest work delivery
rather than rasterization or compilation explaining most of that interval.
Renderer stage scopes overlap; their values must not be added together.

The first 20-second CPU sample straddled recovery, so it cannot identify CPU
cost specifically during the slow window.

Run `20261012-055840-270` slowed before reaching the squad. The requested
frame-8820 capture still shows the initial carrier view; the later automated
route ended against nearby geometry. This is a separate carrier slowdown,
not a second successful reproduction of the squad trigger.

A six-second CPU sample automatically triggered on a pump window with 16
presents, 1458 ms waiting, 461 ms FIFO and 58 ms presenting. Over 6.017 seconds,
process CPU totaled 17578 ms. Tracked thread lifetimes account for 12891 ms;
4688 ms remain unattributed, including missed short-lived jobs. The dominant
tracked SPU worker used 5594 ms CPU, with instruction samples in image 28
functions `3C7C`/`3B70` and MFC/atomic paths. Main PPU used 1703 ms and render
thread 1234 ms. Stack samples include waits and are not CPU percentages.
The sampler briefly suspends threads; sampled intervals are not FPS benchmarks.

Wait logging was enabled after this CPU sample. In a subsequent 15.112-second
window (frames 9407–9595), main guest thread 1 spent a measured lower bound of
7754 ms inside service calls. Its condition-variable-7 wait (`sys_cond_wait`,
syscall `0x6B`) at LR `00896C10` accounted
for 4404 ms; several 330-us worker-poll sites at LR `0068EFF0` accounted for
another 2906 ms. These include scheduling delays. Nested scopes are unioned
per thread and calls below 200 us are omitted. This supplies concrete wait
sites to investigate alongside the busy SPU worker; bypassing these waits
without establishing their completion semantics would not be a valid fix.

Local evidence: `local/mission-transition-frame-summary.json`,
`local/mission-transition-cpu.json`, `local/squad-stall-trigger.json`,
`local/squad-stall-cpu.json`, `local/squad-stall-wait-summary.json` and the two
run directories. Raw logs, samples, generated game code and captures remain local.

No performance or rendering fix is claimed by these observations. Next work
is to reproduce the reported null-read/1–2 FPS interval with targeted tracing,
identify the condition-variable signal producer and SPU completion dependency, and validate
any change with repeatable uninstrumented before/after measurements.

## Targeted conversation run and condition producer

Run `20261012-061815-592` used main `6dde832`, SDK `45faa42` plus the bounded
null-read context diagnostic and complete condition signal recording. The
explicit 1440p/follow-shadow settings remained fixed. Captures verify the title,
load confirmation, carrier, approach, objective completion at frame 7969, and
the squad conversation at frame 8283. The route after gaining player control
was forward 700 diagnostic polls, diagonal left/forward 450, then forward 220,
with captures between each movement. Poll counts are not a simulation clock.

This run reached repeated 1.0–2.6 FPS windows during the conversation. In frames
8325–8360, 36 frames took 21.410 seconds (mean 594.72 ms/frame). Mean draw CPU
was 38.66 ms, GPU 2.02 ms, vertex packing 13.40 ms, texture decoding 7.94 ms and
fence waiting 4.23 ms. Shader compilation and PSO creation were zero, as was
limiter waiting. These stage scopes overlap. The suspect `00AD8D00` null read
did not occur, so it is not necessary for this observed slowdown; whether the
user's particular null-read path aggravates it remains open.

The main PPU thread spent a lower bound of 18.436 seconds inside services over
that 21.410-second interval. Condition 7 at LR `00896C10` accounts for 12.779
seconds across 37 calls. The signal producer is guest thread 37: it broadcasts
condition 7 with syscall `0x6D` (`sys_cond_signal_all`) at LR `008A171C`.
Generated code confirms the call to `00911130` after that worker's work phase;
the waiting main-thread wrapper `00911198` uses `sys_cond_wait`, not a semaphore.

Worker 37 spent a lower bound of 20.759 seconds inside services in the same
interval. Four `cellSpursQueuePopBody` completion queues account for:

| Queue | Calls | Service wall ms |
| --- | ---: | ---: |
| `014E3980` | 254 | 5908.20 |
| `014E3000` | 254 | 4484.24 |
| `014E6900` | 242 | 4480.35 |
| `014E4300` | 238 | 3116.10 |

These are already native HLE queue operations over the shared guest ring. They
can wait for real SPU producers, lock-line ownership, or the blocking poll's
`Sleep(1)`. The trace establishes the dependency, not which of those consumes
the queue call. Next profiling must split those costs and identify producer
jobs before replacing scheduling or queue behavior. Returning fabricated
completion would invalidate the simulation.

A separate six-second CPU sample was taken earlier in the slow conversation,
not during frames 8325–8360. It saw 10109 ms process CPU, 8047 ms attributed and
2063 ms unattributed, with persistent image-28 worker CPU 4641 ms. Only 85
sample iterations completed, so the interval is heavily perturbed and is not
a benchmark. It supports identifying the worker, not an FPS gain or a complete
CPU breakdown.

Wait recording was disabled at 06:26:43 UTC around frame 8524. Subsequent
five-second reports were 3.2, 5.4, 14.7 and 18.4 FPS. The scene and conversation
were progressing, so this is not a controlled estimate of recorder overhead
or proof that disabling recording fixes the game. A different agent's
shadow-lighting instance started at 06:27:29 UTC, after the low-rate interval;
later performance is contended. Only the owned profiler was closed, normally
at 06:27:56 UTC with exit 0. All runs remained muted.

Local evidence adds `squad-context-slow-main.json`,
`squad-context-slow-producer.json`, `squad-context-cpu.json`, and
`squad-context-waits/`. The original report's semaphore label was corrected;
`scripts/summarize_waits.py` now resolves numeric syscalls using the checked-out
runtime's table, separately from HLE NIDs. Regression tests cover the actual
condition/semaphore/usleep IDs, kind separation and nested wall-time union.

`PPU_NULL_READ_STACK=<hex guest function>` optionally prints at most four
matching null-read contexts, with QPC and the existing bounded guest stack
diagnostic. It is independent of the generic 16-message cap and remains off
unless requested. Actual muted native run `local/runs/null-context-boot-check`
verified the known boot reader `00010368`, its registers and call-chain output,
and normal exit 0. The conversation run enabled `00AD8D00` and emitted no
matching context. Existing feedback binaries are unchanged by these probes.
