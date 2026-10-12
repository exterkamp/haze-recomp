# Native FIFO acknowledgment wait experiment

The goal remains consistent frame pacing and unlocked gameplay with a VSync
option. Neither broad gameplay timing correctness nor steady carrier performance
is established. Changes below remain on `experiment/native-cpu-kernels` and SDK
`experiment/haze-native-kernels`.

## Caller attribution

The instruction sampler can now unwind native AMD64 frames through Windows
DbgHelp. `scripts/native_stackwalk.c` uses the SDK's structure definitions and
copies each captured CONTEXT before walking. The Python caller resumes threads
in a `finally` block, checks process ownership, and reports total/max suspension
time. Stack walking is disabled by default and must run outside FPS windows.
An explicit symbol directory keeps each candidate paired with its matching PDB.

Build/validate with `scripts/build_profiler.ps1 -Test`. The independent optimized
fixture retains leaf/middle/entry calls without frame pointers or tail calls.
The live oracle recovered 288 correctly ordered chains in three seconds; its
maximum measured suspension was 3.089 ms. The fixture exits normally on its own
deadline. It contains no game-derived code.

Muted ordinary-build run `20261012-175846-901`, candidate SHA256
`55BB364CA52CD4D93D5ECEFCAE2FCDB5DD2A662A3EC84118DFF02D7670005EAF`,
loaded the private carrier checkpoint at 1440p, uncapped/VSync on. Native cursor,
PPU queue and SPU idle waits were enabled. Captures 008454 and 009815 verify
gameplay; normal shutdown returned zero. The twelve-second diagnostic took
527 rounds with depth 16 and a randomized five-ms target interval. Only the
owned game was observed immediately before/after; this was not continuous
process-isolation monitoring.

This aggressive all-thread capture perturbs execution substantially: 15,810
suspensions totaled 8,579.57 ms, with maximum 10.083 ms. RIP/stack counts include
waits and are not CPU utilization shares. They establish call paths, not FPS
benefits or exact wall-time attribution. CPU counters also miss short jobs:
19,890.625 of 46,765.625 process CPU ms were unattributed to opened lifetimes.

On the main thread (host TID 56596, earliest creation timestamp), 147 stacks
included `gcm_fifo_wait_for_ea`, 58 included `sys_timer_usleep`, and 137 had a
memory helper at the top. Those memory calls spread across many routines; the
leading immediate caller had only eight samples. That does not support choosing
one small PPU leaf as the next dominant optimization. The renderer host showed
texture-content hashing, vertex fetching/remapping and shader hashing as active
paths. Its idle stacks also identify `frame_clock` waiting for the command kick.

The FIFO path is concrete: the main thread's reserve callback flushes a tail,
waits for the consumer to reach it, publishes a wrap, then waits for begin.
Both waits currently poll the real acknowledgment with `Sleep(1)`. Replacing
the delay can remove wake latency; it cannot remove the consumer's actual draw
processing or justify pretending commands were consumed.

Artifacts: `local/native-stack-{run,cpu}.json`, before/after process JSONs and
the run captures. Future stack captures should select the relevant threads or
reduce unwind cadence to lower perturbation.

## Experimental native publication/wakeup

SDK commit `29fee8f` makes the consumer acknowledgment an atomic host word and
publishes the actual consumed EA with release ordering. A changed acknowledgment
wakes address waiters. `GCM_FIFO_WAIT_NATIVE=1`, or a live 0/1 file selected by
`GCM_FIFO_WAIT_CONTROL`, replaces the producer's one-ms sleep with WaitOnAddress.
The wait compares its observed value, then rechecks the requested EA with acquire
ordering. Publish-before-wait, unrelated changes and spurious wakes therefore
cannot release an unconsumed ring. A one-ms fallback retains periodic kicks and
handles native-wait errors; no commands or ownership checks are bypassed.

Mode events carry the producer host TID and QPC timestamp. The present-window
reporter now validates `fifo_recycle_wait` against explicitly selected producer
TIDs, rejecting missing, late, wrong-thread, wrong-mode or changed-in-window
events. The sampler and this diagnostic mode do not change graphics settings.

The full ordinary-build/regression suite passed. The existing FIFO test now runs
both modes and retains its 2.3-second deliberately stalled consumer, two-phase
tail/wrap ownership, pending callback, flip insertion and semaphore tests. New
cases prove that an unrelated acknowledgment plus a spurious wake cannot finish
the requested wait, and that an already-published acknowledgment finishes
without another notification. The candidate is:

`local/candidates/native-fifo-wake/HazeRecomp.exe`, SHA256
`DB1420836F66B2762729605D0994D24A5B6F3993A10678667A491D532FB01147`.

Build/test log: `local/native-fifo-wake-build-tests.log`. The native path remains
off by default until ordinary-build gameplay comparisons establish a benefit.

## First ordinary-build comparison

Run `20261012-180932-320` used the candidate above, native cursor/queue/idle
paths enabled, 2560x1440 rendering, VSync on and no host FPS cap. A private copy
of the carrier checkpoint and input file kept this test separate from other
agents. Captures before and after confirmed gameplay. Four 45-second windows
followed five-second warmups, with no sampling, builds, captures or input inside
the windows. Producer TID 24752's native mode events validated each transition.

| Native FIFO wait | FPS | Mean ms | p95 ms | p99 ms | Max ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| Off | 25.16 | 39.74 | 52.84 | 61.60 | 96.31 |
| On | 26.38 | 37.91 | 52.91 | 59.15 | 256.37 |
| On | 27.27 | 36.67 | 51.92 | 60.67 | 96.26 |
| Off | 22.97 | 43.54 | 58.45 | 66.25 | 88.36 |

The two-second process observer saw only the owned Haze throughout the first
pair; its strict coverage check passed. Another agent's game appeared near the
end of the third window and remained through the fourth. The first pair was
selected by process coverage, not by its FPS results. That pair's approximately
4.8% increase is encouraging, but one comparison does not establish a repeatable
gain. Its p95 barely changed and its worst frame was longer. Scene evolution
and concurrent activity also prevent interpreting the full table as proof of
improved frame pacing. A reversed-order run is needed before promotion.

Artifacts: `local/native-fifo-{run,windows}.json`,
`local/native-fifo-presents.csv`, `local/native-fifo-processes.jsonl`,
`local/native-fifo-contended-report.json`, and the explicitly selected
`local/native-fifo-first-pair-{windows,report}.json`.

## Reversed-order repeat

Run `20261012-181957-930` used the same binary/settings with a fresh private
checkpoint copy and order on/off/off/on. Captures 009581 and 014255 confirmed
the carrier before and after measurement; the game closed normally with exit
zero. Producer TID 34072's mode events validated all four 45-second windows.
The strict process-observation coverage check passed for every block: only the
owned Haze was observed, with maximum gaps 2.13..2.15 seconds. Other applications
and processes living entirely between observations are still not excluded.

| Native FIFO wait, reversed | FPS | Mean ms | p95 ms | p99 ms | Max ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| On | 22.89 | 43.68 | 62.30 | 69.83 | 105.54 |
| Off | 21.36 | 46.82 | 65.19 | 91.26 | 155.83 |
| Off | 20.21 | 49.49 | 68.47 | 77.65 | 93.12 |
| On | 22.92 | 43.63 | 61.42 | 70.11 | 91.44 |

Both enabled windows beat both disabled windows for average FPS and p95 frame
time in this repeat. Averaging the two mean frame times per mode gives 22.91
versus 20.77 FPS, about 10.3% higher throughput. Averaged per-block p95 values
are 61.86 versus 66.83 ms; these are not pooled distribution percentiles.
Together with the first pair, this is stronger evidence for a useful native
wakeup path than the earlier shader-cache experiment. It is not a universal
10.3% speedup claim: the animated scene evolves, absolute performance varies
between launches, and this covers one checkpoint. Stable 30 FPS and consistent
frame pacing remain unresolved. Keep the change opt-in pending broader scenes
and longer correctness runs; do not change real command-consumption ownership.

Artifacts: `local/native-fifo-reverse-{run,windows,report}.json`, its presents
CSV, process-observation JSONL, and the run's native event log/captures.
