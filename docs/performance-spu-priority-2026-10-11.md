# Persistent SPU worker scheduling probe

## Question and method

The earlier triangle-index investigation found a persistent SPU worker spending
almost a full CPU core in image 29's queue routines. Matching queue head/tail
words did not prove that it was safe to delay the worker. Inspection of the
compiled routines `3B70`, `3C7C` and `3E68` confirms that the loop includes
conditional atomic writes, not just reads of an idle flag.

Muted native-resolution run `20261012-045602-695` uses main `1285f08` and SDK
`7723eab`, with normal rendering, CPU-work 1283, window-position correction one
and the 30 FPS ceiling. Carrier capture 8124 verifies gameplay before sampling.
A separate 20.032-second instruction sample identifies persistent thread 60204:
18,750 ms of CPU, with the largest symbol groups in the compiled `3C7C` and
`3B70` routines. Total process CPU is 78,140.625 ms, of which 53,328.125 ms is
attributed to observed thread lifetimes; short-lived jobs remain a coverage gap.
RIP samples include waiting threads and are not CPU utilization percentages.

The new diagnostic `scripts/benchmark_thread_priority.py` then alternates only
that thread's Windows priority between normal (0) and below-normal (-1). Its
handle is checked against the explicit executable's process, retained against
ID reuse, and restored afterward. Guest instructions and atomic behavior are
unchanged. Six blocks each use ten seconds of warmup and twelve seconds of
measurement. No sampling, builds or captures run during those blocks. CPU and
QPC measurements are recorded, and the ordinary successful-present ring is
flushed by closing the owned process normally. Only this Haze instance appears
in process checks at the measurement boundaries.

## Result: reject lowering the worker's priority

| Pair | Normal worker CPU cores | Below-normal worker CPU cores | Below-normal complete present intervals | Longest below-normal interval |
| --- | ---: | ---: | ---: | ---: |
| 1 | 0.926 | 0.110 | 25 | 2,644.865 ms |
| 2 | 0.952 | 0.109 | 20 | 2,775.885 ms |
| 3 | 0.909 | 0.129 | 26 | 2,983.791 ms |

Whole-process CPU consumption increases substantially during below-normal
blocks. Normal blocks also vary, with 93, 219 and 157 complete present intervals.
The standard report rejects this session because the required 120 intervals
per block are not available. These short, severely stalled blocks do not supply
a statistically useful FPS/p95 comparison or a new steady-performance baseline.
They do provide direct evidence against shipping this scheduling change: the
worker loses CPU time while rendering experiences repeated multi-second stalls.
This is consistent with dependencies/priority inversion; the exact dependency
still needs tracing. It does not establish a safe idle-worker classification.

The original thread priority is restored. Capture 10473 verifies the stationary
carrier after the experiment. The process closes normally with exit code zero.
No production scheduling policy changes. In particular, the new user feedback
build includes none of this experiment.

Local evidence: `merged-native-cpu.json`, `idle-worker-priority-blocks.json`,
the run's `presents.csv` and its captures. The recording is complete and records
successful priority restoration. Three actual Windows tests cover wrong-process
rejection, retained-handle priority restoration, and an injected recording
failure after priority mutation. Presentation-window rejection tests also pass.

For repetition, identify a fresh persistent worker through sampling first;
thread IDs do not persist across launches. With a verified stationary scene:

```powershell
python scripts/benchmark_thread_priority.py --pid <owned-pid> --tid <sampled-worker-tid> --exe build/haze/HazeRecomp.exe --output local/priority-blocks.json
```

Close the owned game normally before reporting. Longer blocks are necessary for
formal frame-distribution comparisons when this probe causes severe stalls.
