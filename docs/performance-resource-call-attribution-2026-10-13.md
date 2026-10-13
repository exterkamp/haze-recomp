# Nested rendering-resource attribution

Tracking: #71 and #32. Follow-up to the [171-ms resource stall](performance-fifo-wait-outcomes-2026-10-13.md).
This instruments the unresolved operation; it does not claim a speedup.

The existing opt-in slow-draw recorder now appends call counts, summed elapsed
time, maximum call duration and the maximum call's resource identity for:

- Target and MRT surface resolution.
- Sampled depth snapshots.
- Sampled texture slot resolution.
- Vertex texture slot resolution.

These call buckets are disjoint within the existing resource-resolution stage.
Their sum plus `resource_residual_ms` partitions that stage. The residual
includes depth-resource lookup/creation (`zdepth_get`), descriptor handling,
inter-call bookkeeping and any other uninstrumented work. Nested detail must
not be added to the four existing top-level stages.

The slowest identity includes location, offset, format, dimensions and slot.
Surface formats use DXGI values; texture formats use their respective RSX
descriptor values. Surface/depth slots are cache slots, while sampled/vertex
texture slots identify the binding unit. Depth snapshot format is reported as
zero. Target-surface timing wraps the original `current_surface()` call;
identity collection occurs after its end timestamp. Zero-duration first calls
still retain an identity. Counts and maxima reset with each draw scope.

Absent `RSX_SLOW_DRAW_CSV`, the call sites use the original resource functions
and add no QPC queries. The recorder retains the existing bounded ring and
normal-exit CSV dump. There is no new graphics setting or release opt-in.
The analyzer accepts legacy CSVs and requires a complete appended schema when
resource fields are present; it rejects negative/nonfinite timing, impossible
maxima, empty buckets with time, and inconsistent stage partitions.

Validation:

- `tests/test_rsx_slow_draw_ordinary.py` compiles the actual recorder in ordinary
  and `YZ_PERF_PROFILE` fixtures. Both pass disabled-clock, deferred-output,
  nested sum/max/identity and residual checks. Output is private in
  `build/rsx-slow-draw-resource`.
- `python -m unittest tests.test_slow_draw_analysis`: 13 tests pass, covering
  legacy/new CSVs, malformed fields and accounting consistency.
- Full ordinary game build (`scripts/build.ps1 -SpuFma -Jobs 2`) exits 0;
  log `local/resource-phases-build.log`. Candidate is
  `local/candidates/resource-phases/HazeRecomp.exe`, 122,767,872 bytes, SHA-256
  `C1CF212D382D3A1873DDCFD99E3C742A5F83B346E78807ABAAE74DC4BDA76766`.
  SDK commit `fc0587ede45c1a9eddfb7c801417b1194c12f768`; renderer source SHA-256
  `8474D979E7CEC338B885DD31A301454E01D38EADBE06CDA281BA8E85B316427D`.

## Live validation

Muted run `20261013-231542-903`, PID 9692, resumes a private copy of the jungle
save at 1440p/8192 shadows, VSync off and uncapped. A fresh private copy of the
shader-bundle cache is used. Injection and a transient tutorial message finish
before the verified scene capture and input detach. Two 45-second absolute
camera sweeps use tokens 3500/3501, normal FIFO timeout 1, unchanged native
controls, and all three detailed renderer diagnostics. Normal close exits 0;
the original save baseline hashes match.

| Window | Frames | FPS | Mean ms | p95 ms | p99 ms | Max ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 3500 | 2733 | 60.757 | 16.459 | 28.034 | 40.309 | 88.292 |
| 3501 | 2715 | 60.387 | 16.560 | 27.215 | 43.865 | 81.157 |

Pooled: 5,448 frames, **60.572 FPS / 16.509 ms mean / 27.737 ms p95 /
41.485 ms p99**; 124 intervals above 33.333 ms and 20 above 50 ms. All frames
are retained. Mean pump partition is wait 9.215 ms, FIFO 5.114 ms, presentation
1.985 ms, other 0.196 ms. The 88.292-ms worst frame spends 79.956 ms in repeated
pump waits, 5.939 ms in FIFO, and 2.188 ms in presentation; it is not another
long texture-resolution callback.

Mode/camera checks pass. Strict isolation fails: Haze PID 49304 overlaps and
up to three compiler processes are observed. Those compilers are not the owned
build, which had already exited. Resource sampling brackets the windows with
20 observations, maximum gap 5.406 seconds, and memory use 54.99-56.19%.
No owned compilation, screenshots or heavy analysis occurs during timing.
Combined with the extra diagnostic clocks, these conditions prevent using
this run as a release-FPS comparison or attributing the lower FPS to a change.

All 62 qualifying draw records are retained without overwrite and the new
schema passes the analyzer's nested-partition validation. Only one callback
above 4 ms falls in the timed windows: 4.652 ms total, of which geometry takes
4.626 ms and resource resolution 0.008 ms. The earlier 171-ms resource hitch
does not recur.

The strongest resource records occur **before the timed windows**:

| Callback | Frame | Resource stage ms | Largest sampled-texture call ms | Texture dimensions | Binding |
| --- | ---: | ---: | ---: | --- | ---: |
| 1272485 | 30394 | 13.4973 | 13.4943 | 128 x 128 | 0 |
| 1244007 | 30344 | 10.9461 | 10.9420 | 16 x 16 | 2 |
| 1514783 | 30893 | 6.9917 | 6.9848 | 512 x 512 | 5 |

Each has an existing PSO hit and zero shader compilation/disk load. In these
records a single `texture_srv_slot` call owns the long resource interval;
surface lookup is at most 0.0001 ms and no depth/vertex-texture calls occur.
This identifies the next instrumentation boundary, not the cause inside the
texture call. Raw draw-thread cycles are 217,610 / 1,815,610 / 1,543,010;
they are not converted to CPU milliseconds. Driver calls, logging and
descheduling remain possible. Large stalls during the measured sweeps instead
require producer/wait investigation; these texture records do not explain them.

The flush ring retains all 1,365 events; the FIFO ring retains 16,384 and
overwrites 2,300 older events. Private evidence is
`local/jungle-resource-phases-a1-{comparison-evidence,pump-report,draw-analysis,
slow-analysis}.json`, raw CSVs, camera/window records and observer logs. This
validates the diagnostic in-game; no performance acceptance is claimed.

The existing draw thread-cycle count and slow-flush QPC joins remain available.
A long elapsed resource call is not automatically CPU conversion work: driver
blocking, logging and scheduler delays still require evidence.
