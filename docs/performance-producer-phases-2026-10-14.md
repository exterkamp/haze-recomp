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

## Resolved callbacks and nested dispatcher cost

Follow-up code `d21609b` adds the six indirect sites and 14 dispatcher direct
sites, using active TLS parentage for nested calls. The ring grows to 1,048,576
records; disabled calls without an active parent remain clock/file free.
Nine runtime fixtures and four source-patch tests pass. Offline analyzer
`76a5c59` groups nested calls by parent function, return site and target without
adding their inclusive time to root totals; seven analyzer tests pass.

Candidate `local/candidates/producer-callbacks/HazeRecomp.exe` has SHA256
`81EB2A55EEC13B4E1753924741E5FBCC18E40A5EE4F6F13E0D5DC510A62C842E`,
same SDK `7574134`, two-job build exit zero. Muted private-save run
`20261014-020816-216`, PID 57596, retains the preceding settings and enabled
producer trace. Two 20-second camera windows use tokens 3760/3761:

- `14559361146564..14559561151434`
- `14559611160904..14559811165169`

All 3,684 present intervals give **92.154 FPS / 10.851 ms mean**, p95 13.721 ms,
p99 15.745 ms, worst 20.245 ms, none over 33.333 ms. This remains an instrumented
diagnostic, not a demonstrated gain. Camera/mode/present and Haze-process checks
pass. The resource-pressure observer does **not** bracket the final 2.385 seconds:
closing after two seconds was too early for its approximately five-second sample
interval. Ten retained samples show no compiler/other Haze and 53.83–54.09% memory,
but full resource coverage fails. Preserve this limitation rather than trimming
the measurement. The private report now emits this explicit failed coverage
instead of raising on a missing end bracket; subsequent runs use a longer close
delay. No owned build, screenshot or heavy analysis ran during timing.

The ring retains both windows, with no overlapping discard, late completion,
overwritten in-flight scope or invalid clock. Two scopes unfinished at dump
begin after the windows. Normal close exits zero; baseline save hashes match.

Across 3,688 overlapping roots:

| Parent / return site | Resolved target | Clipped wall ms per root |
| --- | --- | ---: |
| Main / `00681184` | `005FBB70` | 4.318 |
| Main / `006812F8` | `006869F0` | 4.354 |
| **Inside `006869F0`** / `00686A50`, argument 2 | `00684ED8` | **4.187** |
| Main / `0068128C` | `00697C70` | 1.485 |
| Inside `006869F0` / `00686AC8` | `004D2828` | 0.131 |

The nested 4.187 ms is part of the 4.354 ms dispatcher time, not an additional
frame cost. Only about 0.025 ms/root remains outside the main direct/indirect
scopes. Whole-call means for `00684ED8` are 4.195/4.184 ms; p99 7.693/7.621 ms.
The dominant main callback `005FBB70` averages 4.331/4.309 ms per whole call.

Cross-checking the earlier 3,903-sample producer stack trace narrows these paths:
1,234 of 1,520 samples containing `005FBB70` also enter its nearest guest child
`0050B7B0`; 1,434 of 1,519 containing `00684ED8` have nearest guest child
`006DE730`. Completion waits occur inside both paths in that older capture.
Counts are not CPU shares, and that stack capture incurred 5.785 seconds of
suspension. This supports testing the guarded early-completion policy against
the measured phases before attempting a blind native rewrite of either registry
or object-update loop. No new runtime policy is enabled by these diagnostics.

Evidence: `local/jungle-producer-callbacks-a1-*`, plus
`local/producer-hotphase-stack-crosscheck.json` derived from the earlier retained
timeline. The next same-binary diagnostic enables only `HAZE_NATIVE_JOB_WAIT=1`
and keeps phase tracing enabled to test this hypothesis.

## Early-completion phase comparison

The same callback candidate, with only `HAZE_NATIVE_JOB_WAIT=1` changed, runs
muted from a new private save/cache as `20261014-021712-970`, PID 9572.
The two 20-second windows use camera tokens 3780/3781:

- `14564727252796..14564927256016`
- `14564977267051..14565177270187`

| Instrumented policy | Frames | FPS | Mean ms | p95 ms | p99 ms | Worst ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Original delay | 3,684 | 92.154 | 10.851 | 13.721 | 15.745 | 20.245 |
| Early completion | 3,817 | 95.472 | 10.474 | 13.207 | 15.455 | 23.449 |

All frames are retained; neither run has a frame over 33.333 ms. Mean frame
time is 0.377 ms lower with early completion, but the worst frame is higher.
Pump means change from command wait/FIFO/present 5.558/3.390/1.840 ms to
5.218/3.407/1.785 ms. This is an observed diagnostic difference, not proof of
stable pacing or a release speedup: tracing is enabled, NPC/simulation state is
not replayed exactly, and the original-delay capture lacks the final resource
bracket described above. The earlier pre-fix mixed-load test is not pooled with
this comparison.

The parent-preserving phase analysis shows small reductions in the expected
main-thread regions, measured per root intersecting each window:

| Region | Original ms/root | Early completion ms/root |
| --- | ---: | ---: |
| Main callback `005FBB70` | 4.318 | 4.152 |
| Dispatcher `006869F0` | 4.354 | 4.288 |
| Nested event-2 `00684ED8` (included above) | 4.187 | 4.119 |
| `00697C70` | 1.485 | 1.324 |

The whole-launch shutdown snapshot reports 585,288 handled delays, 475,865
queue-ready early returns, 109,423 expirations, zero primary-ready returns and
zero fallbacks. These counts establish execution of the intended path, not
timed-window call counts or directly measured time saved by individual returns.

Eleven resource samples now bracket both windows (seven-second close delay),
maximum gap 5.354 seconds, memory 53.38–53.78%, no competing Haze/compiler
observed. Camera, runtime-mode, present and Haze-process checks pass. No owned
build/screenshot/heavy analysis overlaps timing. The phase ring retains both
windows with no overlapping discard or invalid QPC; shutdown is normal, exit
zero, and all baseline save hashes match. Evidence prefix:
`local/jungle-producer-ready-a1-*`.

Disposition: keep early completion disabled pending an ordinary tracing-off
comparison and gameplay acceptance. The experiment supports a small reduction
in completion-delay overhead, while most of the roughly 10.5 ms frame remains.
The next larger investigation follows `0050B7B0` under the main callback and
`006DE730 -> 006AC9A8` under event-2 dispatch, where the older stack capture
places both useful work and completion waits. Attribute current native-job
execution, queue servicing and submission costs there before replacing an
entire object/registry walk. Consistent pacing, 144 FPS and full unlocked/VSync
acceptance remain open; no WIP promotion follows from this diagnostic.
