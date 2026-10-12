# Audio producer wait and uncapped clock observations, 2026-10-12

## Measured lead

The separate CPU sample after the FI squad benchmark identified guest thread 8,
host TID 64624, using 5015.625 ms CPU in 6.003 seconds. Many instruction samples
landed in timer/delay services. These counts include waits and do not attribute
that CPU cost to a particular instruction.

Watchdog records name LR `007E078C` with r3 `0x320` (800 microseconds).
The lifted caller `007E0708` invokes `00B73738`; that function's `00B739E8`
loop requests an 800-us `sys_timer_usleep` until its audio ring progress allows
the next block. This differs from the 30-us completion-word loop in `006F0208`.
Changing the latter would not address this measured audio worker.

The runtime already has an opt-in reusable high-resolution timer backend
controlled by bit 8 in `PS3_USLEEP_MODE_FILE`. The default short sleep yields
to a QPC deadline; the timer alternative parks the host thread. Earlier
experiments had not demonstrated better FPS. This change adds native host-TID
and QPC mode events, including initial state, so benchmarks can reject an
unsettled or changing backend on explicitly selected workers. It does not
change sleep durations, thresholds, backend selection or the default.
SDK telemetry commit: `59fbc8d` on the `haze` fork.

`benchmark_present_modes.py --mode-column usleep --flags 0 8 8 0` now validates
these events. Regression fixtures cover missing, late, wrong-worker,
wrong-mode and within-window changes for both modes. The complete build/test
gate passed:

```powershell
./scripts/build.ps1 -SpuFma -Test -OutputDirectory local/candidates/timer-measure
```

Log: `local/timer-measure-build-tests.log`. Candidate SHA256:
`6B312771B3BF1E17F74289063D8799636C456589C834C93B6ED6FD7DECF36C56`.

## Unprofiled carrier comparison

Muted run `20261012-102043-901`, owned PID 38764, used 2560x1440 rendering,
follow-resolution shadows, AA off, filtering 1, a 1280x720 window,
`frame_limit=0` and `vsync=off`. The native log confirms `present_sync=0`.
Both native PPU queue waits and SPU idle wait were enabled from startup.
The profile/checkpoint was copied to a separate save directory and loaded.
Captures 151123 and 159803 verify the same stationary carrier view before and
after the four blocks; NPC activity still changes over time.

Each block measured 60 seconds after five seconds of settling. No input,
screenshots, builds or instruction sampling ran inside these windows. Reports
verify every consecutive present, native mode events on audio worker 63440,
and observations bracketing each entire window. Only the owned Haze was
observed, at most 2.13 seconds apart; this does not exclude other applications
or very short-lived processes between probes.

| Backend | Frames | FPS | Mean ms | p95 ms | p99 ms | Worst ms | Process cores |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Yielding poll | 1553 | 25.90 | 38.61 | 50.77 | 57.32 | 248.82 | 4.20 |
| Reusable timer | 1528 | 25.49 | 39.23 | 52.48 | 58.38 | 74.21 | 3.48 |
| Reusable timer | 1546 | 25.80 | 38.77 | 51.44 | 57.75 | 79.92 | 3.57 |
| Yielding poll | 1540 | 25.70 | 38.92 | 52.58 | 59.71 | 80.34 | 4.53 |

The timer reduces CPU use but does not demonstrate improved FPS or p95/p99
pacing. The first polling block's worst stall alone is insufficient evidence
of a repeatable tail-latency improvement. Keep the backend opt-in. Consistent
30 FPS is not achieved.

Local evidence: `timer-uncapped-presents.csv`,
`timer-uncapped-carrier-windows.json`, `timer-uncapped-carrier-report.json`,
`timer-uncapped-processes.jsonl` and the run directory. The game closed normally
with exit 0 at 10:36:45 UTC. All measured carrier blocks survived the bounded
16384-present ring; the report rejects missing intervals rather than shortening
windows when data is overwritten.

## Separate audio worker CPU comparison

After all FPS blocks, four eight-second worker CPU/cycle intervals alternated
poll/timer/timer/poll, with one second of settling. A retained read-only thread
handle verifies host TID 63440 belongs to owned PID 38764 and is still alive;
no priority or affinity was changed. Native mode events also identify that
worker's selected backend. These are not additional FPS measurements.

| Backend | CPU ms in eight seconds | Mean cores | Thread cycles |
| --- | ---: | ---: | ---: |
| Poll | 6671.875 | 0.834 | 31839307760 |
| Timer | 125.000 | 0.0156 | 578540860 |
| Timer | 109.375 | 0.0137 | 556489870 |
| Poll | 7000.000 | 0.875 | 31906705760 |

Raw results: `local/timer-worker-cpu.json`. This confirms removable host busy
waiting in the audio producer, while the carrier A/B shows that removing it
does not remove the current frame bottleneck.

## Guest clock investigation

New read-only tool `scripts/sample_haze_clock.py` resolves `vm_base` from the
owned process's matching PDB and checks its executable path. It samples the
candidate clock globals identified by lifted routines `0068ECD8` and
`0068EC78`, checking the BLUS30094 TOC base and expected field pointers before
reading. It neither suspends threads nor writes guest state. Samples taken
while its before/after frame counters differ remain marked in the raw output;
equal counters do not guarantee an atomic snapshot of the entire update.

The routine computes a delta from a live clock, applies optional correction
and scaling, adds it to an accumulator, and increments its frame counter.
Its observed minimum-delay constant was zero, its delta threshold was 0.1,
and its correction bound was 5. These names and units are reverse-engineering
leads; verifying every gameplay/animation consumer remains outstanding.

Ten seconds of separate carrier sampling (`timer-uncapped-carrier-clock.json`)
gave 490 counter-stable observations. Endpoints span 9.9969612 seconds of wall
time and 244 counter updates; the accumulator advanced 10.0010133 and the last
clock 10.0034810. Sampled delta ranged 0.035227..0.046318, median 0.041244.
This supports a time-based clock at about 24 FPS rather than a fixed 1/30
increment in this routine, without proving all simulation timing is correct.

After camera input, capture 166010 verifies a sky-facing view. A separate
three-second sample (`timer-uncapped-sky-clock.json`) had 147 counter-stable
observations spanning 2.9952962 seconds, 89 updates (29.71 Hz), accumulator
advance 2.9915533, and delta median 0.033259. The same interval contains 89
actual presents. This is a brief diagnostic, not a timed FPS benchmark or
evidence of gameplay above 30 FPS. Correct higher-rate gameplay and VSync-on
behavior still require runtime validation. The full goal remains active.
