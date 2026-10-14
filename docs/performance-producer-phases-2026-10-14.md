# Main-thread phase attribution at the real jungle checkpoint

Tracking: [#71](https://github.com/zdiemer/haze-recomp/issues/71),
[#35](https://github.com/zdiemer/haze-recomp/issues/35),
[#39](https://github.com/zdiemer/haze-recomp/issues/39).

The first phase capture identifies two large regions previously hidden behind
renderer command waits. It does **not** demonstrate a new speedup. One indirect
callback region consumes about 4.34 ms per main update; direct dispatcher
`006869F0` consumes another 4.38 ms. These are inclusive wall intervals, including
any nested execution or waits, not proven removable costs.

## Implementation and validation

Commit `ab748e7` instruments 59 direct call sites in main update `00680FA0`,
including trampoline drains. Fifteen calls to repeated timer helpers `00680D18`
and `0068EC78` are excluded. Source fingerprints and a strip/round-trip test
guard the original and complete build-local patch pipeline. Guest operations,
branching, registers and indirect calls remain intact. The runtime is inactive
unless `HAZE_PRODUCER_PROFILE_CSV` is set, and does no per-frame file I/O.

The bounded 524,288-record ring tracks begin/completion identities, nesting,
QPC intervals, raw thread-cycle deltas, overwritten rows and in-flight scopes.
Normal shutdown freezes it without waiting for guest threads. Analyzer tests
cover nested interval unions, clipping, lifecycle conservation, invalid clocks,
lost parents and QPC-only pump correlation. Seven runtime fixture tests, three
patch tests and six analyzer tests passed for this capture's implementation;
the full candidate build exited zero with two compiler jobs.

Candidate `local/candidates/producer-phases/HazeRecomp.exe`:

- Haze `ab748e76633bebda056eefcd32b18716451af19c`, SDK `7574134df9e0f243b147220917031abcef5059ee`.
- SHA256 `90137A8D12C3B15BA377DF1F8CFDD868E3C66D0BC6A7A9EFCE525089666B069A`.
- Build log `local/producer-phases-build.log`, identity `local/producer-phases-identity.json`.

## Capture conditions and timings

Muted run `20261014-015352-384`, PID 11412, uses a private copy of the real save,
fresh private shader cache, 2560x1440, 8192 shadows, FXAA/AF16, uncapped/VSync off.
Existing CPU/GPU modes are 13571/2, VP batching 1, persistent workers enabled.
Native slot copy, native job-delay probing, bulk fill, inline reads/coherence,
milestones and broad renderer/job tracing are off. Only the narrow producer
trace is enabled alongside existing present/pump timing. Boot reporting is now
confirmed **off** in the runner result; its cost is not established by this run.

Visual setup checks confirm the jungle scene, empty Nectar gauge and cleared
tutorial prompt. Diagnostic PAD_FILE input is detached before measurement.
Two 30-second absolute camera sweeps use tokens 3740/3741, QPC frequency 10 MHz:

- `14550649583350..14550949585963`
- `14550999600027..14551299604154`

All 5,505 present intervals yield **91.768 FPS / 10.897 ms mean**, p95 13.690 ms,
p99 15.492 ms, worst 22.291 ms; none exceeds 33.333 ms. Individual sweeps are
91.593 and 91.944 FPS. Pump means are command wait 5.701 ms, FIFO 3.305 ms,
present 1.829 ms, other 0.062 ms. These diagnostic numbers are not a release
benchmark or a matched comparison with the previous 95.6 FPS copy-OFF run.

Strict camera/mode/present checks pass. Fourteen resource samples bracket the
measurement, maximum gap 5.321 seconds, memory 53.04–53.42%, no other Haze or
compiler observed. No owned builds, screenshots or heavy analysis overlap the
timed windows. Normal close exits zero; all seven baseline save hashes match.

## Attribution and remaining uncertainty

Across the two windows, 5,507 completed root scopes overlap the windows,
including clipped boundary scopes. Their union covers 59,997.608 ms of the
60,000.674 ms requested; root scope IDs are not joined to pump frame IDs.

| Region | Mean wall ms per overlapping root |
| --- | ---: |
| First indirect callback region, after LR `00681148`, before LR `0068119C` | 4.343 |
| Direct call `006869F0`, LR `006812F8` | 4.381 |
| Direct call `00697C70`, LR `0068128C` | 1.476 |
| Other direct calls and unprofiled root work | 0.695 |
| Whole main update, clipped at window boundaries | 10.895 |

The first callback region totals 23,915.130 ms; its maximum observed gap is
10.658 ms. It accounts for almost all of the 24,229.443 ms not covered by direct
call scopes. This region includes descriptor lookup, guards and dispatch; it
is not yet a separately timed callback body. The second indirect callback
region averages only 0.035 ms. No raw cycle counts are converted to CPU time.

The worst pump interval is 22.291 ms, with 16.346 ms command wait. A `006869F0`
scope overlaps 11.343 ms of that interval. Other scopes and two main updates
also overlap it: the asynchronous pipeline prevents treating an overlap as a
one-to-one frame dependency or summing producer time onto renderer frame time.

The ring retained the complete timed region: the last discarded completed
scope ends at `14550227357836`, before either window; no overwritten in-flight
scopes, late completions or invalid QPC observations were recorded. Two scopes
are unfinished at dump, both begin after the windows. Whole-process coverage
is false because startup rows were overwritten. The CSV lacks a capture-start
timestamp, so the standalone analyzer conservatively does not certify the
recording lifetime; retained preceding rows and the external run/window records
support this particular capture's timed coverage.

## Next replacement boundary

Source inspection shows `006869F0` walks a registry through multiple passes and
dispatches synchronous callbacks. Neither the registry's immutability nor
callback independence is proven, so caching/skipping/reordering it is premature.
The next trace records the six dynamic callback sites in the two functions and
the 14 direct calls within `006869F0`, retaining hierarchy and target addresses.

Ghidra's read-only disassembly confirms the two main `bctrl` return addresses
`00681184`, `006811DC`, and four dispatcher returns `00686AC8`, `00686BE8`,
`00686C5C`, `00686CFC`. The first main callback descriptor is reached through
`r30-0x7FD8`, with `r30` loaded from `[TOC-0x7D70]`; it is not directly
`TOC-0x7FD8`. Resolved targets need runtime evidence before domain names or native
replacement contracts are asserted.

Private evidence prefix: `local/jungle-producer-phases-a1-*`, including raw
producer/pump/present CSVs, per-window phase reports, gap analysis, camera and
resource checks. Generated game code, disassembly and save assets remain local.
