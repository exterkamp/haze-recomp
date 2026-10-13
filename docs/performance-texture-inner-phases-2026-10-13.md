# Texture-call attribution and remaining command waits

Tracking: #71, #59 and #32. These are diagnostic results, not a verified FPS gain.

## Candidate and run

Haze `31bab7a` pins SDK `c64652d`; the ordinary RelWithDebInfo build has no
YZ_PERF_PROFILE and uses SPU FMA/native float. Two compiler jobs completed before
launch. Candidate `local/candidates/texture-inner-async/HazeRecomp.exe` is
122,774,016 bytes, SHA-256
`193D3E9B8C9E9B83F3DC303C0D588A66FAC248188A4D7B1FCBE726B202A341D0`.
Build log and identity: `local/texture-inner-async-{build.log,identity.json}`.

Muted run `20261013-234948-365`, PID 5200, resumes a private real-save copy at
2560x1440, 8192 shadows, uncapped/VSync off. Shader caches are copied. Native
job wait, bulk fill and inline reads/coherency are off; CPU mode 13571, GPU pull
2, VP burst 1 and worker reuse 1 are fixed. Milestone logging is off. Deferred
slow-draw/FIFO/flush diagnostics are on, with a 4-ms slow-draw threshold.
The jungle rifle scene is visually checked after the Nectar effect ends; input
file polling is detached before two 45-second sweeps, camera tokens 3560/3561.

All 7,021 measured intervals are retained:

| Measurement | Result |
| --- | ---: |
| Window FPS | 77.524 / 78.537 |
| Pooled FPS | 78.031 |
| Mean frame ms | 12.815 |
| p95 / p99 ms | 17.006 / 21.311 |
| Maximum ms | 39.048 |
| Frames above 33.333 / 50 ms | 2 / 0 |
| Mean wait / FIFO / present ms | 6.168 / 4.645 / 1.908 |

Strict isolation fails: other Haze PIDs 43680/57724 and up to one compiler
overlap. Nineteen resource samples show 53.40-55.97% memory, but the largest
10.090-second gap fails the 8-second coverage threshold. No owned build,
screenshot or heavy analysis runs during timing. Added diagnostic clocks and
host variation prevent causal comparison with the logging-disabled baseline.
The game exits normally and original save hashes match.

## What the inner timing establishes

All 64 slow callbacks are retained without overwrite; nested phase partitions
validate. One retained callback falls in each measured window. The slowest
texture call inside the second window is callback 6367336, renderer frame
49017, QPC 14476461977477..14476462019386:

- Texture cache hit, location 0, offset 73154944, format 136, 2048x2048, slot 1.
- Entire callback 4.1909 ms; maximum texture call 4.1436 ms.
- Hash/content comparison 4.1274 ms, write-watch 0.0149 ms, lookup 0.0006 ms,
  residual 0.0007 ms. No decode, resource creation, descriptor write, retirement
  or direct texture diagnostic logging in this call.
- Whole-callback thread-cycle delta is 1,098,390. This is not CPU milliseconds;
  elapsed hash time can include preemption, and cannot all be claimed as
  removable hash computation.
- QPC containment places the callback inside pump frame 49015 (the counters
  differ): 31.7126 ms total, 17.5246 ms waiting, 12.2067 ms FIFO, 1.9208 ms
  presentation. This callback does not explain the whole frame.

Earlier, before measurement, callback 1462381 has a 3.7154-ms texture hit with
3.7101 ms in write-watch (256x256, offset 94249856, format 136). The whole
callback costs 5.4274 ms and 3,573,410 thread cycles. Startup frame 364 instead
shows a 12.0709-ms miss: 1.5549 ms hash and 10.5011 ms decode, including
0.6496 ms resource creation. Startup decode work and steady-state hit checks
are separate leads.

The worst measured pump interval remains wait-heavy: frame 51094 takes
39.0475 ms, including 25.5881 ms wait, 11.4204 ms FIFO and 1.9013 ms present.
The texture sample above is not in that frame. Next work should inspect content
validation and producer availability without attributing all remaining pacing
variation to either path.

Evidence prefix: `local/jungle-texture-inner-a1`, including raw draw/FIFO/flush,
pump/present/camera records, windows, process/resource observations, validated
reports and failed strict gates. No game assets, saves or generated code are
committed.
