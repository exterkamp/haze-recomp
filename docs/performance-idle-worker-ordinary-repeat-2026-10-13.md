# Ordinary three-versus-five workload comparison

The five-workload extension has **not demonstrated a repeatable frame-pacing
gain**. Four ordinary-build runs, ordered A1/B1/B2/A2, retain every measured
present interval. The candidate's first run stalls substantially despite no
observed compilers and low memory pressure. Its faster repeat and the second
baseline contain external compiler observations, so this is not a complete
isolated ABBA acceptance result. The existing user WIP is not replaced.

## Method and binaries

Each run launches a new process with a fresh copy of the same real jungle
checkpoint. Tutorial dismissed, Nectar activated, scene visually verified;
1440p, 8192 shadows, VSync off, uncapped, native job-0 kernel enabled. Two
24-second absolute 30-degree camera sweeps per run, each after a five-second
warmup. Worker reuse remains enabled throughout; A allows three exact workload
fingerprints, B allows five. No heavy job/draw profiler, compilation owned by
this agent, screenshot or offline trace analysis runs in measured windows.
Audio muted; all four processes close normally. Seven baseline save hashes
remain unchanged.

| Variant | Candidate | SHA-256 |
|---|---|---|
| A, three | `idle-worker-reuse-default` | `CB9052BB4848C5628C24571BCB2E9AC1AEF748B5A455B7BD8F328B81FE7028E4` |
| B, five | `idle-worker-five-ordinary` | `A6ACBC36AE4C5D9022D2750C8A86DCB783754C3A4A4F373331D1CB0FC7D883A7` |

B contains SDK 5108fbe; the slow-draw diagnostic is compiled out of this
ordinary build. A is the existing three-workload release candidate. This is
a comparison of whole binaries, not a same-process toggle of the added jobs.

## All observed results

| Run | Intervals | Mean ms | FPS | p95 ms | p99 ms | Max ms | >33.333 ms | >50 ms | Observed compiler maximum |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| A1 | 4048 | 11.850 | 84.386 | 14.932 | 17.290 | 33.499 | 1 | 0 | 0 |
| B1 | 2992 | 16.031 | 62.378 | 20.862 | 72.139 | 597.403 | 83 | 48 | 0 |
| B2 | 4199 | 11.422 | 87.551 | 14.586 | 16.832 | 21.224 | 0 | 0 | 2 |
| A2 | 4103 | 11.692 | 85.530 | 15.039 | 20.065 | 282.054 | 11 | 7 | 2 |

Every run passes mode/camera/present/Haze-process audits. Only its owned game
is observed. Resource samples bracket the windows with maximum gaps below
5.335 seconds. A1 memory: 47.52–48.78%; B1: 47.35–47.73%; B2: 47.65–48.36%;
A2: 48.12–48.98%. Compiler snapshot QPC confirms activity inside both B2
windows and A2's second window. These runs are retained and explicitly marked
contended; no assertion that compilation caused a particular stall follows.
Sampling cannot exclude activity between observations or other applications.
The observed window rectangle/priority stays unchanged in A1/B1/B2, with the
same foreground process across those samples.

Pooling all raw intervals, **including the contended runs**, gives A mean
11.771 ms / 84.958 FPS, p99 18.359 ms; B mean 13.340 ms / 74.964 FPS,
p99 37.952 ms. These are descriptive summaries, not an isolated causal
estimate. Neither selecting only B2 nor discarding B1 is justified.

B1's longest pump interval is 619.6824 ms: 518.5320 ms FIFO processing,
74.1023 ms outer waits, 24.9124 ms present and 2.1343 ms other work. This
interval is not identical to the 597.4029 ms present gap because the clocks
bound different scopes. The pump reports zero thread CPU time at coarse OS
accounting resolution; it does not prove zero work by renderer/worker threads.
Other large B1 pump intervals also accumulate substantial FIFO wall time.
Internal waits, descheduling and actual rendering work remain unseparated.

## Decision and evidence

Do not promote the five-workload extension as a verified speedup. It remains
experimental. Next work should attribute large FIFO stalls and the separately
observed shared job completion/cache-spill pattern, then evaluate an explicit
fix against the same checkpoint. Merely repeating runs until a favorable
number appears would not establish acceptance. Tracking #9, #32 and #36 stay
open; stable pacing and broader gameplay validation are unfinished.

Private evidence prefixes and launch IDs:

- `local/jungle-three-a1`: `20261013-111101-232`, tokens 2180–2181.
- `local/jungle-five-b1`: `20261013-111622-091`, tokens 2200–2201.
- `local/jungle-five-b2`: `20261013-112631-458`, tokens 2220–2221.
- `local/jungle-three-a2`: `20261013-113120-200`, tokens 2240–2241.

Each prefix retains raw presents/pump/camera/process/resource observations,
measurement windows and validated report/pooled summaries. Combined raw
interval summary: `local/three-five-ordinary-comparison.json`.
