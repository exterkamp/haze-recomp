# Guest frame-pacer investigation

The native settings exposed an unlimited host frame rate, but lighter gameplay
views still approached 30 FPS. The candidate clock at `0068ECD8` had a zero
minimum-delay constant. That observation did not exclude another frame wait.

## Updated job profile

Muted diagnostic run `20261012-104747-852`, candidate SHA256
`CF788A092284ED2932DB45AC60F093F7104FE01700E91ABBEDBA52C27DB7C954`,
used 2560x1440 rendering, follow-resolution shadows, AA off, AF1, host cap zero,
VSync off, FMA, native PPU queue waiting and native SPU idle waiting. No other
Haze was running at launch. The initial carrier view is captured at 54868 and
55862; NPCs continue moving. The game closed normally with exit code zero.

Frames 55400–55850 span 17.925547 seconds. Completed-job timing recorded 56,142
jobs; persistent jobs without a return are excluded. CPU-cycle scopes overlap
and are not elapsed milliseconds. Full job logging adds contention, so these
are diagnostics rather than an ordinary-build FPS comparison.

| Runtime image | Jobs | Native cycles, billions | Mean ABI wall time, ms |
| --- | ---: | ---: | ---: |
| 42 | 1123 | 20.519 | 4.112 |
| 40 | 449 | 20.145 | 9.801 |
| 30 | 30083 | 8.046 | 0.095 |
| 41 | 5400 | 9.569 | 0.416 |
| 33 | 1796 | 8.751 | 1.080 |

All completed jobs total 94.028 billion ABI cycles and 86.478 billion inner
execution cycles. Explicit setup totals 4.085 billion and submitting-thread
creation 5.382 billion. Heap contexts are already the current default. Images
42 and 40 account for 47.0% of measured inner execution cycles. This strengthens
the case for examining their translated work/helpers before allocation alone;
it does not prove that either job is the entire frame's critical path.

The same diagnostic averages 21.629 ms draw CPU, 6.952 ms vertex preparation,
3.382 ms texture hashing, 3.947 ms PSO preparation and 2.107 ms GPU time, with
zero shader compilations. These scopes overlap. Rendering has substantial CPU
work even when GPU time is short.

Separate filtered-image run `20261012-105204-032` sampled runtime image 42,
compiled image `spu_0041_at_00F2E380`. A six-second diagnostic obtained 442 RIP
samples, including 57 in `000035F8`, a loop copying three words through LS
rotate/shuffle operations, and 31 in `0000359C`. DMA/channel/overlay helpers also
appear. These are sampled instruction locations, not CPU utilization shares.
Raw stack candidates for 24 of 31 `NtWaitForSingleObject` observations include
`LdrShutdownThread` and `RtlExitUserThread`; they are consistent with teardown
waits rather than evidence of a blocked DMA job. Candidates are not unwound
frames. The optional sampler now retains them, with symbol work after resume.

Artifacts: `local/current-job-profile-{phases,cpu,renderer-summary}.json`,
`local/current-job-profile-renderer.csv`, `local/image42-current-{samples,stacks}.json`.

## Separate main-loop wait

`00680FA0`, called by `00723CE4`, performs the gameplay update and then a
separate pacing decision. TOC slot `E94EE0-7D70` resolves to `E35AE0`.
Object `0169F484` supplies selectors at `+18D`, `+1C4` and `+1C5`; double
`01028B90` stores the previous frame clock. The nonzero wait-selector path
enters `00681358`, queries another setting, and targets either 0.04 seconds or
`0.1 / selector_1C4`. Remaining time above 0.002 seconds goes through the guest
sleep routine; the tail spins until the remainder reaches 0.0001 seconds.
The final clock sample and bookkeeping follow at `006812DC`.

Read-only ordinary-build run `20261012-110052-114` confirmed selectors
`18D=0, 1C4=3, 1C5=1` during carrier gameplay. In the main menu `1C5=0`.
The 30 FPS candidate is therefore separate from the earlier zero minimum in
`0068ECD8`. The file `pacer-discovery-sky-clock.json` was mislabeled during
capture: its matching capture 97737 is still the carrier view; it must not be
used as evidence of a sky-facing performance result.

## Opt-in experiment

`HAZE_NATIVE_FRAME_PACER=1`, or a 0/1 file named by `HAZE_FRAME_PACER_FILE`,
bypasses only the final guest sleep/spin branch. The original update, renderer
submission, final clock sampling and frame bookkeeping remain. Existing PC
frame-limit and VSync settings still apply. This is disabled by default while
higher-rate gameplay timing is validated.

The build patches an ignored copy of the translated source. A SHA256 guard
rejects a changed main-loop function; runtime guards check the expected TOC
globals. Tests reverse the inserted statements and compare the entire source,
reject mutations to both clock update and wait code, and reject double patching.
The full ordinary-build regression suite passed; the new patch tests and native
mode-event benchmark rejection cases passed separately after that build.

The ordinary candidate is `local/candidates/guest-pacer/HazeRecomp.exe`, SHA256
`92C8888465E7A1558ED9922983A9CC6D7C1AE13DAE90FFD71489BDA6A1E1858E`.
## Ordinary-build measurements

Muted run `20261012-110854-216` used the same 1440p settings above, without job
or renderer timing instrumentation. Native mode events from main host thread
10372 validate each block. Two-second process observations bracket all sky
blocks and show only the owned Haze; they do not exclude unrelated applications
or another process entirely between observations. The game closed normally.
Capture 143348 verifies a sky-facing view before measurement; no input, capture,
or thread sampling occurs inside the timed windows. Five warmup seconds precede
each 30-second block, ordered original/bypass/bypass/original.

| Sky block | FPS | Mean frame, ms | p95, ms | p99, ms | Mean CPU cores |
| --- | ---: | ---: | ---: | ---: | ---: |
| Original 1 | 29.795 | 33.563 | 36.507 | 47.224 | 3.871 |
| Bypass 1 | 36.387 | 27.482 | 33.593 | 50.171 | 4.622 |
| Bypass 2 | 37.993 | 26.321 | 33.325 | 46.862 | 4.735 |
| Original 2 | 29.961 | 33.377 | 36.402 | 42.747 | 3.805 |

This demonstrates gameplay presentation above 30 in this lighter view, with
higher CPU use. Tail stalls remain; removing a wait does not establish consistent
frame pacing or a comparable gain in more expensive scenes.

A separate three-second, read-only clock sample with bypass enabled recorded
107 update-counter increments in 2.990435 wall seconds (35.781 Hz), 2.989977
seconds of accumulated game time, and 108 presents between its QPC endpoints.
The one-present discrepancy and non-atomic clock sampling preclude asserting an
exact update/present mapping. Sampled delta values span 24.322–36.030 ms.
Advancing the sampled clock with wall time does not validate every gameplay,
physics, animation, audio or networking system at a higher frame rate.

Artifacts: `local/guest-pacer-sky-{windows,report}.json`,
`local/guest-pacer-abba-{presents.csv,processes.jsonl}`,
`local/guest-pacer-unlocked-sky-clock.json`.

The earlier carrier windows in the same run lost their beginning when the
16,384-entry present ring wrapped before shutdown. The report now rejects
unbracketed CSV windows even if enough later frames remain. No full carrier
comparison is claimed from that truncated recording. Repeat run
`20261012-112647-372` completed its four 30-second blocks but the launcher
timeout killed the process before normal-close CSV flushing. Its CPU windows
alone cannot establish FPS and are excluded. Future benchmark commands should
schedule normal close directly after their final measurement/observer bracket
instead of depending on a later agent turn before the launcher timeout.

Consistent frame pacing, correct simulation above 30, and VSync-on verification
remain outstanding; the full goal is active.
