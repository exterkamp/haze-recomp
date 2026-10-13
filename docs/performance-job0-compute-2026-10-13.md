# Job-0 native compute experiment

Tracking: #66, under #37 and #32. This replaces arithmetic call boundaries in
two live compute loops; it does **not** complete the whole-job replacement.

## Live inputs and contract

A muted diagnostic run (`20261013-090556-501`) resumed a disposable copy of the
real jungle checkpoint at 1440p, 8192 shadows, uncapped and VSync off. The scene
and dismissed Nectar tutorial were visually checked before enabling capture.
Candidate SHA-256:
`73B3BEDF9FCC827F0B4B18605666E187FF2FB071ABE698F27F1873FCAC818F0A`.

The bounded recorder captured four input snapshots at each of SPU image 30
PCs `03408` and `036F0`. No snapshot arrived for the full-precision variant at
`03F48`. These eight snapshots come from a short burst, not broad scene or
campaign coverage. They remain private in `local/job0-fixtures-jungle-20261013`.

- `03408` converts 84 transform records in each captured call, reading 64-byte
  records and producing 16-byte values. It uses the original reciprocal-square-
  root estimate/refinement path.
- `036F0` handles packed transform inputs: two items per 48-byte input block,
  producing two 16-byte outputs. Captured counts are 122, 242, 680 and 122.
- The compute blocks contain no DMA, channels, indirect calls or callbacks.
  Input/output local-store access order, all written registers and final
  continuation are preserved. No assumption about dead scratch registers is
  needed. The surrounding job's transfers and completion are unchanged.
- Full-slot ingress is a `0x1450`-byte GET. The one-byte access at slot `+0x1440`
  is a **PUT**, not the GET incorrectly described in the earlier trace note.
  The complete whole-job contract still needs auditing.

Fixtures contain raw registers and 256 KiB local store, with an explicit
little-endian header, PC/thread/QPC identity and payload hash. Capture stores at
most four records per site, never replaces existing files, freezes publication
at shutdown, and writes only completed records. Ordinary builds omit it.

## Experiments and selected path

Simply moving temporary registers into compiler locals did not provide a
useful gain with the existing hardware arithmetic calls. On real packed-loop
inputs it often regressed substantially. That variant is retained only in the
offline comparison harness.

The selected variant keeps temporaries local **and** inlines the existing
hardware arithmetic into an AVX-512-targeted compute function. Feature/mode
selection happens at the boundary. Each arithmetic operation retains the
existing toward-zero rounding, zero/underflow handling, exponent-risk checks,
and independent integer fallback for risky lanes. Byte operations and estimate
refinement semantics remain unchanged.

The original lift is retained for unsupported CPUs, disabled hardware
arithmetic, observers, a different image/PC, policy mode, non-owned local store,
or an existing pending trampoline. The runtime arithmetic validator also
forces the original path. Only the two captured sites are integrated; `03F48`
remains an offline experiment. `HAZE_JOB0_NATIVE=0` and the internal
`HAZE_JOB0_NATIVE_FILE` permit matched comparisons. No graphics-menu option or
user opt-in is added; the supported path is selected automatically.

In one alternating offline cycle measurement, the inlined path reduced the
packed loop's cycles by 25-34% and conversion-loop cycles by 40-43%. Subsequent
measurements varied; these are narrow kernel measurements, not whole-frame
savings. The important next gate is an ordinary-build matched camera sweep.

## Validation

- 1,288 full-state comparisons passed across synthetic inputs, both arithmetic
  modes, the exact integrated wrappers, and all eight captured inputs. Every
  register, local-store byte and continuation matched the original translation.
- 2,533,120 arithmetic-lane comparisons passed against the independent integer
  reference, including edge values, mixed risky lanes, rounding modes and
  FTZ/DAZ settings. MXCSR remained unchanged.
- Recorder tests cover activation, bounded admission, payload hashes,
  existing-file preservation, and capture racing shutdown with 24 producers.
- Three patch tests cover unchanged guest operations for capture, exact lift
  fingerprint rejection, repeated-patch rejection, and restricting live native
  hooks to the observed sites. Extraction emits game-derived code only locally.
- The ordinary build and full `build.ps1 -SpuFma -Jobs 2 -Test` suite passed.
  Actual Ninja rules confirm profiling hooks are excluded. Build parallelism
  now defaults to two; explicit overrides remain available.

Private logs: `local/job0-inline-real-fixtures.log`,
`local/job0-integrated-real-fixtures.log`, `local/job0-inline-arithmetic-test.log`,
`local/job0-capture-test.log`, and `local/job0-inline-build.log`.

Ordinary candidate SHA-256:
`E6F60F490F7909B42888119E0652AC0885C57CF46B077E61055A87930CA98550`.
SDK remains `6cc2923817e1212ea865611287b0e965fdb29d3b`.

The supported-CPU fixture checks do not establish broader gameplay correctness,
stable 144 FPS, or the whole-job contract. Those remain open.

## Ordinary-build camera-sweep comparison

Run `20261013-092323-549` used the same ordinary executable for four 24-second
windows in original/optimized/optimized/original order, each with five seconds
of settling and the same absolute 30-degree camera sweep. Hardware arithmetic
was active in both modes. Runtime logs confirm both integrated loops executed.

| Window | Mode | Frames | FPS | Mean ms | p95 ms | p99 ms | Max ms |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | Original | 1576 | 65.71 | 15.219 | 19.046 | 23.322 | 37.509 |
| 2 | Optimized | 1637 | 68.25 | 14.651 | 18.111 | 22.789 | 36.923 |
| 3 | Optimized | 1650 | 68.80 | 14.536 | 18.137 | 23.293 | 31.838 |
| 4 | Original | 1588 | 66.24 | 15.096 | 18.906 | 22.778 | 48.388 |

Pooling the original windows gives 15.157 ms / 65.976 FPS; pooling optimized
windows gives 14.593 ms / 68.526 FPS. Mean interval decreases **0.564 ms (3.72%)**
and throughput increases **3.86%**. Pooled p95 improves from 19.009 to 18.135 ms;
pooled p99 is effectively unchanged, 23.056 versus 23.055 ms. Lower maxima in
this short comparison do not establish a reliable reduction in rare stalls.

Camera, settled-mode, present continuity and Haze-process coverage checks pass
for all four full windows. Resource observations bracket the comparison:
24 samples, maximum sample gap 5.364 seconds, memory 46.46-47.01%, no compiler
processes observed. Only the owned Haze process was observed. Other background
activity and processes shorter than the sampling gap are not excluded. There
were no builds or screenshots during recording. The game closed normally.

A resource-monitor path error occurred before the first recording attempt;
that attempt produced no measurement windows. Its process observations were
preserved separately before recording the successful comparison on the same
game instance. No timing window or frame dip was removed from the report.

Evidence: `local/jungle-job0-inline-{windows,report,pooled}.json`,
`local/jungle-job0-inline-{presents,camera.txt}.csv`, process/pressure JSONL,
and the run's boot log. This is one balanced comparison with two windows per
mode. Broader scene coverage and a separate repeat remain follow-ups; the p99
tail and whole-job replacement remain unresolved.
