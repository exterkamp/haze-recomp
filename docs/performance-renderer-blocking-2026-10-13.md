# Renderer blocking and diagnostic overhead

Follow-up to [job-loop attribution](performance-job-critical-path-2026-10-13.md),
tracked in [#9](https://github.com/zdiemer/haze-recomp/issues/9) and
[#12](https://github.com/zdiemer/haze-recomp/issues/12). These are diagnostic
findings, not a new ordinary-build FPS improvement.

## Corrected long-frame attribution

The earlier 169.891 ms pump interval already had a nested **143.663 ms PSO
preparation** scope in its raw renderer CSV. The summary's fixed field list
omitted that scope. Root `72e7696` retains every available renderer `_ms` scope;
all 12 pump-summary tests pass. PSO creation itself was 3.924 ms and shader
compilation zero. The particular blocking/preemption operation inside that
preparation interval remains unidentified. The scopes overlap and must not be
added.

SDK `8280897` measures shader disk load/store, blob creation and blob lookup
separately without changing cache behavior. Blob creation is nested in disk
load. Profile candidate SHA256:
`8A2DBB382C34013AE763256667130E8E072AF807B494082BE142077F63824528`.
Its incremental profile build passes; the prior complete regression suite is
recorded in the job-loop report.

## Shader I/O diagnostic

Run `20261013-043309-160` uses 1440p rendering, shadows following rendering,
1280x720 window, VSync on, cap zero, GPU occurrence mode 2, CPU flags 13571,
native VP consumer on, producer and shortened job polling off. Audio processing
is real and muted. Two 24-second carrier camera sweeps follow five-second
warmups. Present, camera, mode and sampled process audits pass. No other Haze
was observed; host CPU activity averaged 54.7%/54.4%.

| Diagnostic present intervals | Sweep 1 | Sweep 2 |
|---|---:|---:|
| Frames | 769 | 805 |
| FPS | 32.11 | 33.63 |
| Mean ms | 31.144 | 29.739 |
| p95 ms | 43.282 | 40.230 |
| p99 ms | 47.651 | 44.233 |
| Maximum ms | 63.672 | 59.120 |

Shader disk load averages only 0.0039/0.0075 ms per complete pump interval.
The earlier 144 ms preparation stall did not recur. One pump interval has
47.323 ms in presentation with zero shader disk load; pump and present CSVs
use different boundaries. During initial scene entry, outside these camera
windows, three frames spend 79–172 ms creating pipelines versus 5–11 ms
loading shader data. This supports investigating first-use pipeline creation,
but does not establish that shader preloading fixes steady-state pacing.

## Renderer stack capture

The first attempted renderer sample selected the guest main thread by thread
age. That capture is not renderer evidence. `profile_native_cpu.py` now has
`--window-thread`, selecting the unique `ps3recomp_null` window belonging to
the requested Haze process, then retaining thread process/lifetime checks.

Run `20261013-044450-284`, same candidate/settings, records window 4981858,
PID 41416 and renderer TID 56944. Over 16.005 seconds it takes 1,194 samples,
suspending the renderer for 669.371 ms total (8.482 ms maximum). Its frame
times are perturbed and excluded from ordinary performance comparisons.

Observed stacks include:

- 28 samples in `WriteFile`/`fflush` from `ld_profile_present`.
- 10 samples waiting for a CRT lock in `fprintf` from `ld_profile_present`,
  plus six similar pump-report stacks.
- 26 samples in a driver synchronization wait under `create_texture_mipped`.
- 22 samples in driver allocation destruction under `ld_flush`.
- Command-arrival waits and flush fence waits also appear.

These observations identify synchronous diagnostic output as an additional
source of renderer stalls and resource allocation/retirement as a runtime
lead. They do not establish CPU percentages, removable milliseconds, or the
cause of the earlier particular PSO stall. Windows ETW collection could not
start because the OS refused the profiling privilege; no scheduling trace
was collected.

Next gates: buffer diagnostic output off the timed path and revalidate its
accounting; measure resource creation/retirement and synchronization in
ordinary matched builds before choosing pooling/reuse changes. Continue
producer-readiness work in #39. Persistent PSO caching remains unimplemented.
The delivered user WIP is unchanged.

Private evidence: `local/shader-io-profile-*`, `local/render-window-stacks.json`
and `local/render-window-sample-*`. Raw game captures and generated sources
remain uncommitted.
