# Confirmed jungle checkpoint: settled gameplay baseline

The user confirmed the jungle Nectar tutorial as the intended real-save resume
point. The original save remains untouched. This run uses a disposable copy of
the verified private snapshot, finishes the tutorial through gamepad input,
and measures two camera sweeps with the tutorial overlay dismissed.

## Setup and validation

- Run `20261013-052854-871`, private candidate `deferred-profile`.
- Executable SHA-256 `376B0C1C5672F74E59B196AE985120394072A7CFEF17B77DA5B7E643787CA49F`.
- 1440p, native shadow resolution, VSync on, frame cap zero; GPU geometry
  occurrences and native indices enabled; native VP consumer on, producer off.
- Real audio pipeline muted. No CPU sampling, build, or screenshot capture
  during the measured windows. Deferred renderer/pump diagnostics enabled.
- Two 24-second windows, each after five seconds of warmup, with the same
  absolute 30-degree camera sweep. The player remains at the checkpoint;
  NPC/world state is live. This is not a combat traversal benchmark.
- Actual-angle audit passes in both windows (16 phase bins, maximum yaw error
  below 0.00000012 radians). Mode and consecutive-present checks pass.
- Process audit passes: only the owned Haze was observed during the windows,
  with observation gaps below 2.14 seconds. Other host applications and process
  lifetimes between observations are not excluded.

## Results

| Metric | Sweep 1 | Sweep 2 |
| --- | ---: | ---: |
| Complete frame intervals | 1,639 | 1,665 |
| Mean FPS | 68.34 | 69.41 |
| Mean frame time | 14.634 ms | 14.407 ms |
| 95th-percentile frame time | 18.993 ms | 19.010 ms |
| 99th-percentile frame time | 23.473 ms | 23.025 ms |
| Maximum frame time | 152.102 ms | 34.157 ms |
| Pump wait scope, mean | 7.742 ms | 7.707 ms |
| Pump FIFO processing, mean | 4.874 ms | 4.774 ms |
| Pump present scope, mean | 1.914 ms | 1.823 ms |
| Pump other scope, mean | 0.104 ms | 0.104 ms |

The disjoint pump scopes partition elapsed time; they do not establish that all
wait time is removable. Nested renderer scopes must not be added to that
partition. Within rendering, vertex preparation averages 0.380/0.381 ms and
texture decode/upload averages 1.021/0.944 ms. Shader compilation, shader disk
loading and pipeline creation are zero in both measured windows.

The 152-ms spike is pump frame 41817 (renderer frame 41819, joined by QPC):
pump FIFO 150.183 ms, nested draw 149.009 ms,
and nested texture decode/upload 146.339 ms. GPU time is 0.794 ms; vertex
preparation is 0.392 ms. This identifies a texture-path stall, not its specific
allocation/decode/upload cause. The thread CPU sample is zero at Windows'
coarse accounting granularity and does not alone identify blocking.

That renderer row uploads 270,848 texture bytes and records two LRU evictions.
The cache is at its 4,096-entry limit in nearby logs. The decode timer includes
temporary allocations, pixel conversion, D3D12 committed-resource creation,
upload staging and copy/barrier recording; it excludes deferred old-resource
release. Resource creation is a strong lead given the small transfer and earlier
driver stack samples, but needs direct phase timing before choosing a fix.

This is a new representative baseline, not a same-scene before/after gain.
144 FPS requires 6.944 ms per frame: these means are about 2.1 times that budget,
with substantially worse tail latency. Initial loading is measured separately.

Private evidence: `local/real-resume-play-{windows,report,summary}.json`,
matching renderer/pump/present CSVs, actual-angle CSV and process JSONL. No
private save, capture, binary or game content is committed.

## Initial-load phase instrumentation

The diagnostic renderer now records separate shader disk-cache load open,
read, close and emitted-progress-log times/counts, plus a residual after
subtracting those phases and blob creation. Store-side logging is excluded.
The code is gated by `YZ_PERF_PROFILE`; it adds no user-facing setting.
These are elapsed scopes, not a distinction between storage service time,
filesystem cache, filter drivers and thread descheduling.

The profile build succeeds. Twelve pump-summary and three present-mode tests
pass. The first sandbox test attempt could not create temporary fixtures;
rerunning with fixture-directory access succeeds. The previous deferred-buffer
commit already passed the full build regression suite; that entire suite was
not repeated for these counters.

Run `20261013-053823-471` stopped at the Continue confirmation menu and is
excluded from checkpoint results. Run `20261013-054054-104` visually verifies
the Continue/OK selection before confirmation and the jungle tutorial after
45 seconds. No capture occurs within that 45-second interval. Candidate SHA:
`9EE3F5633FFA63ED7BF175A92E09BD675F64C76B10ECD678ADD3B7DF7D74FC43`.

Configuration audit: this later run's boot log records 1440p, 8192 shadows
and VSync off. The earlier `052854-871` settled baseline used native shadows
and VSync on. Both had no frame cap. The shared WIP settings changed between
these runs; their configuration must not be treated as identical.

The latter run preserves 32,768 consecutive renderer rows with 99 correctly
aligned columns and zero recorder rejections. All timing columns are finite
and nonnegative. Across retained rows, shader subphases reconstruct the outer
scope to floating-point rounding precision. It retains 5,266 renderer and
5,265 complete pump intervals within the initial 45 seconds. Process sampling
observes only the owned Haze from 0.288 seconds after confirmation through
the end, with gaps below 2.13 seconds; the first 0.288 seconds lack coverage.

| Initial-load scope | Largest-frame value | Whole 45-second sum |
| --- | ---: | ---: |
| Frame interval | 309.023 ms | Not a gameplay FPS measurement |
| Graphics pipeline creation | 174.763 ms | 626.571 ms |
| Texture decode/upload | 53.405 ms | Separate from pipeline time |
| Shader-cache load | 6.651 ms | 23.720 ms |
| Cache file open | 3.992 ms | 14.308 ms |
| Cache reads | 1.812 ms | 6.590 ms |
| Cache close | 0.633 ms | 1.894 ms |
| Emitted load progress logging | 0.008 ms | 0.015 ms |
| Shader compilation | 0 ms | 0 ms |

The largest frame is renderer 65722, 6.405 seconds after confirmation. The
matching pump frame 65719 has 48.821 ms waiting, 255.208 ms FIFO processing,
4.820 ms presenting and 0.180 ms other. Pipeline creation occurs inside FIFO
processing; it must not be added again to that partition. Another frame has
146.309 ms pipeline creation within a 176.656 ms interval.

The earlier 611/741-ms shader-load scopes do **not** recur here. Neither those
stalls nor loading in general is fixed by this diagnostic change. Existing
cache files remain in place and cache/driver state was not reset; this is not
a cold-start comparison. Pipeline creation is a recurring first-use cost;
attribution of the rare shader-load outliers remains open.

Additional private evidence: `local/real-resume-phases-verified-analysis.json`,
marker, process observations, CSVs and captures. The earlier menu-only run is
retained separately rather than included in loading results.
