# Deferred renderer diagnostics

Tracking #71, #9 and #32. The stall-trigger capture in
`performance-pump-stack-timeline-2026-10-13.md` found three CRT logging-lock
wait samples inside a 104.920 ms pump interval, including 95.736 ms in
presentation. Periodic renderer counters were printed every 32 frames on that
thread. This identifies a synchronous logging path, not the lock's owner or
the full cause of that interval.

SDK commits `8ad73a5` and `b7c14f0` move routine `[live-draw]` counter output
to explicit exit dumps and renderer teardown. Periodic counter and `[fps]`
lines remain available under the existing `RSX_LIVE_FEED_DBG` diagnostic mode;
the ordinary default emits neither during gameplay. Counter collection,
renderer behavior, errors, and independent on-screen FPS calculation remain
in place. No new user-facing setting or tuning option is introduced.

The exit-dump call matters: Haze normally invokes
`rsx_live_draw_dump_present_samples()` and then `exit()`, without calling
`rsx_live_draw_shutdown()`. Review caught this after the first build. The
correction reports counters from that existing renderer-thread exit path as
well. This does not add asynchronous logging or remove other synchronous
diagnostics and control-file access; those remain part of #71.

The corrected ordinary profiling-off/FMA build succeeds with two compiler
jobs. Candidate `local/candidates/deferred-renderer-log-final/HazeRecomp.exe`,
SHA256 `0A439B8A23E1B5752B87E16B92F2AC986D66225F74CC1893432C0EB6F04CBAE5`.
Build log: `local/deferred-renderer-log-final-build.log`. Control remains the
32-slot ordinary candidate, SHA256
`37F8F02193F0CACEC97D6AF0D7D2D8BEB8BBEC79756891AF6FA40163286FDB59`.

## Preliminary exit-path failure

Run `20261013-125440-514`, prefix `local/jungle-logdefer-a1`, tested the
initial SDK `8ad73a5` build (SHA256
`4618C3792D2545FE4552906DF8AAFAC160B9F2CE4158E2147051E867712D582E`).
It correctly suppresses periodic output, but normal close lacks the intended
final counter report. Its raw evidence is preserved separately from the
corrected-build comparison. Two 24-second camera sweeps contain 3283 intervals:
14.4902 ms mean / 69.0121 FPS, p95 17.9128 ms, p99 23.9545 ms, maximum
324.7166 ms, ten frames over 50 ms. All camera/mode/present/process checks pass;
memory is 48.46-48.97%, no compilers observed, maximum resource gap 5.373 seconds.

The 324.7165 ms pump interval contains 187.5678 ms FIFO, 133.3735 ms outer
wait, and 1.9635 ms presentation. Another 276.4897 ms interval contains
234.9015 ms FIFO. Large stalls clearly remain after removing these particular
periodic reports. This preliminary run is not discarded as an outlier or used
to claim a speedup; it fails the exit-report requirement and uses a different
binary from the final planned candidate/control/control/candidate comparison.

## Corrected-build comparison protocol

Fresh launches compare deferred/periodic/periodic/deferred logging. Each uses
the same copied jungle checkpoint, 1440p with 8192 shadows, VSync off/uncapped,
five eligible workloads with 32 retained workers, native job-0 compute, and
the same fixed renderer controls. Two 24-second absolute 30-degree camera
sweeps follow five-second warmups. No stack sampler, own builds, screenshots,
or heavy offline analysis run during measurement. Every measured interval is
retained, including large stalls. Live world/NPC state is not deterministic.

An initial control attempt (`local/jungle-logperiodic-b1`, run
`20261013-130337-044`, tokens 2460/2461) started timing before its screenshot
was reviewed. The Nectar tutorial was still open, so that attempt fails the
scene precondition. Its files remain preserved; it is not part of the gameplay
comparison. A fresh visually verified control replaces it. The final candidate
also needs a navigation retry after its preflight screenshot shows the title
screen; no timing window has started at that point.

## Completed ordinary comparison

| Run | Logging | Intervals | Mean ms / FPS | p99 ms | Maximum ms | Over 50 ms |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| A1 `20261013-125959-850` | Deferred | 3412 | 14.065 / 71.10 | 22.023 | 744.671 | 7 |
| B1 `20261013-130732-553` | Periodic | 3510 | 13.640 / 73.31 | 24.911 | 1070.223 | 13 |
| B2 `20261013-131244-912` | Periodic | 3264 | 14.700 / 68.03 | 29.644 | 504.603 | 27 |
| A2 `20261013-131650-233` | Deferred | 3923 | 12.230 / 81.77 | 18.327 | 463.142 | 4 |

All four pass mode, camera, present-interval and owned-process checks and close
normally. Each deferred run emits exactly one final balanced counter report
and no periodic FPS lines. Controls emit 887/960 counter reports and 48/43 FPS
lines. Seven immutable baseline-save files retain their original hashes.
Private evidence: `local/deferred-renderer-log-comparison.json` and the four
`local/jungle-log{defer,periodic}-*` raw captures, reports and resource logs.

Pooling raw intervals descriptively gives deferred 13.083 ms / 76.43 FPS,
p99 20.123 ms, versus periodic 14.151 ms / 70.67 FPS, p99 26.644 ms. This is
not an established causal speedup. The first candidate is slower on average
than the first control, world state is live, and final-candidate preparation
takes substantially longer. B2 also observes up to two external compiler
processes inside both measurement windows; that overlap is retained and its
impact is unknown. No compilers are observed in A1, B1 or A2. Physical memory
use ranges from 49.01% to 51.56% across the comparison; maximum observer gaps
are 5.34-5.51 seconds. These captures do not reproduce the previously reported
94% memory-pressure condition.

A2's navigation retries include a failed screenshot request while the game
remains alive. An eight-second preflight stack capture performs 224 thread
suspensions totaling 950.247 ms, maximum 362.250 ms. This is intrusive loading
diagnosis, not performance evidence. The same process subsequently produces
a fresh gameplay screenshot, clears the tutorial, and completes both timed
windows without a sampler. The longer preparation and diagnostic intervention
remain limitations even though neither overlaps timing. No attempts are
silently discarded as timing outliers.

## Remaining long stalls

The final candidate's 463.142 ms pump interval spends 460.562 ms in FIFO work
and 2.565 ms in presentation. Another 434.831 ms interval spends 422.601 ms
waiting for commands. Its other two intervals above 50 ms are 187.916 ms
(186.104 ms FIFO) and 157.262 ms (147.411 ms outer wait). The first candidate's
744.672 ms interval similarly contains 710.406 ms FIFO, while B1's 1070.224 ms
interval contains 925.955 ms outer wait. These categories include blocking;
they are not measures of useful CPU execution. Quantized thread CPU counters
cannot resolve the exact blocking call or identify its owner.

Removing periodic logs eliminates one observed synchronous path and preserves
exit evidence, but severe pacing failures remain even without compiler
overlap or high physical-memory usage. Do not promote this comparison as a
rare-stall fix or a stable 144 FPS result. Continue #71's control-file audit
and capture producer/consumer/worker state around long FIFO and command-wait
intervals. The existing user WIP is unchanged.
