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
