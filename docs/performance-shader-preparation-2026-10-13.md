# Shader preparation with GPU occurrence decoding

Tracked in [#35](https://github.com/zdiemer/haze-recomp/issues/35), with pipeline
caching in [#12](https://github.com/zdiemer/haze-recomp/issues/12) and remaining
frame attribution in [#9](https://github.com/zdiemer/haze-recomp/issues/9).
Source: root `c958b40` / SDK `6acf5d9`; subsequent root `5b48eea` changes only
the occurrence experiment's evidence report.

The existing vertex-program cache retains instruction count, input/TXL masks
and the original hash. Every hit validates instruction bytes through END; it
does not cache mutable constants or trust guest addresses as identities.
Earlier FPS evidence was mixed and one reverse run was contended. The current
test repeats both orders with the audited camera sweep before deciding whether
this existing path should become a default. No new cache or setting is added.

## Ordinary comparison

Candidate SHA256
`D16063B017DEEFCFFFD333D5E5496BE0B51E29F733E8B1B23F01CEBF1D8D0165`,
run `20261013-002000-386`, private carrier save. Settings: 2560x1440 rendering,
1280x720 window, follow shadows, AA off, AF 1, VSync on, frame limit zero,
muted real audio pipeline. GPU occurrence mode 2 remains fixed; CPU-work flags
5379/5891 differ only by vertex-program metadata-cache bit 512. Geometry/vertex
caches and vertex workers remain off; other native defaults remain enabled.

Each window has five warmup seconds and 24 measured seconds, with six complete
four-second yaw sweeps (+/-30 degrees, fixed position/pitch). World/NPC state
remains live. Both orders run within one process. Captures and compilation occur
outside measured windows. Present, CPU-mode, actual camera and sampled process
coverage checks pass for all eight windows. Only the owned Haze is observed;
other workloads and lifetimes between samples are not excluded.

| Order | VP cache | Frames | FPS | Mean ms | p95 ms | p99 ms | Maximum ms |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| First | Off | 780 | 32.57 | 30.704 | 42.424 | 48.731 | 62.361 |
| First | On | 781 | 32.58 | 30.693 | 42.946 | 49.090 | 110.350 |
| First | On | 814 | 33.99 | 29.423 | 39.773 | 44.373 | 55.846 |
| First | Off | 785 | 32.78 | 30.511 | 41.754 | 47.993 | 58.452 |
| Reverse | On | 705 | 29.39 | 34.025 | 46.628 | 53.933 | 111.493 |
| Reverse | Off | 788 | 32.85 | 30.439 | 41.002 | 46.192 | 53.205 |
| Reverse | Off | 791 | 33.00 | 30.305 | 41.081 | 47.913 | 69.525 |
| Reverse | On | 795 | 33.17 | 30.146 | 40.373 | 45.676 | 75.289 |

The result is mixed: one first-order cached window improves, one is essentially
flat, and the reverse cached windows include a regression. No outliers are
removed. This does not establish a repeatable whole-frame or pacing gain and
does not justify changing the default. Normal close exits 0, with no dropped
draws, texture decode failures or GPU preparation bypasses in the final log.
The cache's native oracle is rebuilt and passes 162,064 byte mutations plus
END/bounds changes, address/content reuse, eviction and guarded tails.

Private records use `local/gpu-vpcache-motion-*`; oracle output is
`local/gpu-vpcache-oracle.log`. The prior occurrence comparison, including
both successful orders, is documented separately in
[the occurrence report](performance-gpu-occurrences-2026-10-13.md).

## Separate stage profile and frame timeline

Profile candidate SHA256
`63DBCDCC6974EEDD7E7298C7789F5B3FF3616B796F9C6DB42F5394572CBF173D`,
run `20261013-002619-449`, uses the same rendering and input settings. The
four 24-second windows alternate off/on/on/off with five-second warmups.
Wait tracing is configured but its live control stays zero during these
windows: no wait records are written, though control polling remains in the
instrumented build. Shader variant tracking is disabled by the existing
`RSX_PROFILE_LIGHT=1` default. Ordinary FPS is reported separately above.

All four windows pass present, mode, camera and sampled process checks. Stage
intervals are consecutive, GPU occurrence mode stays two, and no shader
compilation occurs. The program cache reports about 2,528/2,583 validated hits
and 46 misses per frame in its two enabled windows.

| Scope, ms/frame | Off first | On first | On second | Off second |
| --- | ---: | ---: | ---: | ---: |
| Vertex program preparation | 1.317 | 0.211 | 0.210 | 1.279 |
| Fragment program preparation | 1.321 | 1.328 | 1.278 | 1.283 |
| PSO preparation | 2.961 | 2.047 | 1.998 | 2.857 |
| Renderer draw scope | 11.589 | 11.138 | 10.501 | 11.307 |
| Before first draw | 11.478 | 11.776 | 12.406 | 12.200 |
| First-to-last draw span | 17.203 | 16.575 | 16.088 | 16.631 |
| Draw-span time outside draw scopes | 5.614 | 5.438 | 5.587 | 5.323 |
| After last draw | 1.146 | 1.198 | 1.147 | 1.206 |
| No-draw interval contribution | 0.087 | 0.041 | 0.086 | 0.065 |

The cache removes roughly 1.1 ms from measured VP preparation. VP/FP/PSO and
draw scopes overlap, so their savings must not be added. The timing calls also
differ between cache paths; the ordinary comparison remains the FPS test.

Timeline rows use the previous profile sample, first/last draw timestamps and
current sample. Intervals with no draws are retained separately rather than
dropped. Before-first, first-to-last, after-last and no-draw contributions form
a partition of the recorded interval; draw scope is a subset of the draw span.
These are wall-time regions, not attributed CPU costs. In particular, time
outside draw scopes can include command handling, scheduling, waits and other
work. The profile does not identify all of it as producer starvation.

The substantial pre-first-draw and inter-draw time explains why a renderer
component saving alone is insufficient evidence of stable pacing. It motivates
the separate wait diagnostic below rather than another cache default change.

## Main-thread wait diagnostic

After all cache windows finish, the same process enables `PPU_WAIT_PROFILE`
for a separate 24-second camera sweep with cache off (5379), GPU occurrences
on, and a five-second warmup. No instruction sampling or thread suspension is
used. Recording continues five seconds after the window so buffered main-thread
events bracket it. The owned game then closes normally, exit 0.

The diagnostic passes present, mode, camera and process coverage checks.
There are 768 consecutive profile intervals, frames 13688 through 14456,
totaling 31.211 ms/frame. Guest main thread 1 maps to host TID 22048 throughout
the recorded main-thread data. Service intervals are clipped to frame regions
and unioned so nested calls do not count twice. Main-thread event timestamps
bracket the measured window. Calls shorter than 200 us are omitted by the
recorder, so recorded service time remains a lower bound.

| Frame region | Duration, ms/frame | Overlapping main-thread service union, ms/frame |
| --- | ---: | ---: |
| Before first draw | 12.203 | 2.671 |
| First-to-last draw | 17.838 | 2.433 |
| After last draw | 1.170 | 0.336 |
| Whole interval | 31.211 | 5.440 |

The dominant group is 12,036 calls to `sys_timer_usleep` at LR `0068EFF0`,
requesting 330 us (`arg0=0x14A`). They occupy 5.315 ms/frame overall, including
2.581 ms/frame before the first draw; the longest recorded call is 16.676 ms.
This corroborates the earlier completion-loop lead with the current renderer.
The next group, `cellPadGetData`, contributes 0.100 ms/frame across 22 recorded
calls and includes one 72.011 ms call. The benchmark's pad-file path and host
scheduling are potential contributors; this is not proof of a release-input
bug. All intervals remain in the records.

Service scopes include scheduling delays and recording can perturb the run.
Concurrent main-thread sleeps do not prove why the renderer is waiting or that
all requested sleep time is unnecessary. The pending jobs must really finish.
Source inspection locates the previously identified completion loop at
`00682538`, which checks completion and services pending work between retries.
The targeted native replacement and its guards are now tracked in
[#39](https://github.com/zdiemer/haze-recomp/issues/39) and
[the replacement plan](performance-native-job-completion-plan-2026-10-13.md).

Private profile evidence uses `local/gpu-vpcache-profile-*`, including stage
summary, renderer/geometry CSVs, two window manifests and present reports,
camera audit, process observer, wait streams and wait-overlap summary. The
analysis helpers are `local/summarize-gpu-pull-profile.py` and
`local/analyze-gpu-main-waits.py`. Raw game data remains uncommitted.

## Decision

Keep the VP cache disabled by default: its component saving is confirmed,
but whole-frame improvement remains inconsistent. Preserve the validated GPU
geometry experiments and their broader gameplay gates. Prioritize the measured
job-completion delay under #39 and continue attributing the remaining frame
regions under #9. Stable pacing and 144 FPS remain unproven.
