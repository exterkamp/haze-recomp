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

Live attribution remains to be validated; the implementation tests do not
establish which resource operation causes the observed in-game hitch.

The existing draw thread-cycle count and slow-flush QPC joins remain available.
A long elapsed resource call is not automatically CPU conversion work: driver
blocking, logging and scheduler delays still require evidence.
