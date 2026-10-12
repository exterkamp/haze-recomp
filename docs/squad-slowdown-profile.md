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
7754 ms inside service calls. Its semaphore-7 wait at LR `00896C10` accounted
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
identify the semaphore producer and SPU completion dependency, and validate
any change with repeatable uninstrumented before/after measurements.
