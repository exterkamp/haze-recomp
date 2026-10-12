# GPU vertex decoding experiment

Tracked in [#33](https://github.com/zdiemer/haze-recomp/issues/33), under
[native replacement roadmap #32](https://github.com/zdiemer/haze-recomp/issues/32).
Root branch `experiment/gpu-geometry`; SDK branch `experiment/haze-gpu-geometry`.
This is an experimental replacement, not a stable-framerate claim or release
default. Internal `RSX_GPU_VERTEX_PULL` and `RSX_GPU_VERTEX_PULL_FILE` select
the path for comparisons.

## Boundary and validation

The CPU still gathers references, remaps indices and validates source mappings.
For supported draws, it snapshots merged raw attribute spans and reference
metadata into the existing fenced upload ring. A vertex shader decodes byte
order and formats directly from a raw buffer, supplying the translated shader's
inputs without CPU attribute conversion. Unsupported mappings, inline data,
frequency divisors, geometry-cache mode and incompatible layout options retain
the CPU fetcher. GPU and CPU PSOs have distinct keys. This does not yet remove
CPU index preparation, transient uploads or RSX command reconstruction.

The GPU readback oracle compares 196,608 lanes per run against the CPU fetcher,
covering all seven formats, sizes 1–4, byte alignments, nonzero source bases,
zero stride, index wrapping, defaults, half-float edge encodings, immutable
snapshots and rejected layouts. WARP and hardware both pass. Non-normalized
values are compared bitwise, with NaN classification accepted; normalized
formats permit 1e-6 relative/absolute error. The wrapped vertex entry compiles.
This arithmetic oracle does not establish complete rendering equivalence.

The full `scripts/build.ps1 -SpuFma -Test` gate passes. The ordinary candidate
SHA256 is `765C19EED9AC6FB2FDFBD8D3FF8D0CA57606E8ACE23C6E918BB489351A078659`.
The later expanded oracle and profiling-counter changes do not change this
candidate's decoding behavior; preserve its hash when reproducing these results.

## Ordinary in-game comparisons

Muted run `20261012-232215-759` uses private carrier saves, 2560x1440 rendering,
a 1280x720 window, follow shadows, AA off, AF 1, VSync on and frame limit 0.
Native arithmetic/waits/cursor/texture tracking use the executable defaults;
CPU geometry cache and renderer vertex workers are off. Real audio processing
and device timing remain enabled. The first navigation arrived during startup;
it was repeated after viewing the title screen. A capture during scene fade-in
was not treated as gameplay. Captures 013371 and 014016 verify the carrier with
GPU and CPU decoding respectively before timing.

Four fixed-view windows use five seconds of settling and 25 measured seconds.
No captures, builds or suspending profilers run inside them. Every reported
interval is consecutive and the CSV brackets the complete windows. Process-wide
mode events verify the selected path.

| Decoder | Frames | FPS | Mean ms | p95 ms | p99 ms | Max ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| CPU | 724 | 29.00 | 34.485 | 49.510 | 57.324 | 69.698 |
| GPU | 761 | 30.49 | 32.798 | 44.918 | 49.235 | 77.364 |
| GPU | 781 | 31.25 | 31.995 | 44.991 | 48.553 | 70.992 |
| CPU | 677 | 27.12 | 36.876 | 51.938 | 58.655 | 132.808 |

These windows favor GPU decoding, including the tail percentiles, but show
drift and remain far from consistent pacing or a 6.94 ms interval. Do not
present them as a fixed universal percentage gain.

The first camera-input attempt held full right-stick yaw for successive
10,000-poll commands, with ten seconds of settling and 25 measured seconds per
block. The start view changes (capture 019028), but the final capture 026672
faces a nearby wall. Continuous input alone does not prove a continuous,
matched 360-degree camera trajectory. These results are retained as an
**inconclusive camera test**, not a proven motion/pacing improvement:

| Decoder | Frames | FPS | Mean ms | p95 ms | p99 ms | Max ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| GPU | 1037 | 41.57 | 24.053 | 40.563 | 48.616 | 63.395 |
| CPU | 1154 | 46.21 | 21.642 | 35.437 | 44.865 | 50.606 |
| CPU | 1304 | 52.23 | 19.148 | 24.855 | 28.692 | 47.731 |
| GPU | 1264 | 50.59 | 19.768 | 25.202 | 31.703 | 41.840 |

Both sets pass Haze process-observation coverage: only the owned process was
observed, with maximum gaps below 2.14 seconds. This does not exclude other
applications or short-lived workloads between observations. Normal owned-window
close exits 0. The final renderer counters report no dropped draws, shader
failures or texture decode failures. The GPU path prepared 6,710,224 draws and
1,970,935,127 vertices, uploading 84,954,227,776 bytes, with zero preparation
bypasses across this entire run (including menus and loading). Those cumulative
counts are not per-frame gameplay costs.

Private records: `local/gpu-pull-{stationary,rotation}-{windows,report}.json`,
`local/gpu-pull-smoke-{presents.csv,processes.jsonl}`, the run captures/logs,
`local/gpu-vertex-pull-{oracle,build-tests}.log`. No game data is checked in.

## Separate stage profile

Profiling candidate SHA256
`B0F91053B90BC9E460C9ED9D663AF9FAF185CA914E3515B82022AC0F2636775C`
uses the same decoder and adds cumulative geometry counters alongside the
existing stage CSV. Run `20261012-233141-901` verifies the same carrier view
in capture 008450. It uses four 15-second measurement windows with five-second
warmups, CPU/GPU/GPU/CPU order, no input or captures inside the windows, and
normal close (exit 0). All 400/460/441/409 selected intervals are consecutive,
bracketed by the CSV and mode events, and contain no shader compilation.

| Scope, per frame | CPU first | GPU first | GPU second | CPU second |
| --- | ---: | ---: | ---: | ---: |
| Entire vertex preparation, ms | 7.211 | 4.204 | 4.337 | 7.061 |
| Reference gathering, ms | 1.422 | 1.343 | 1.404 | 1.344 |
| Index remapping, ms | 1.935 | 1.894 | 1.926 | 1.894 |
| Mapping/plan preparation, ms | 0.292 | 0.267 | 0.275 | 0.278 |
| CPU decode or raw snapshot, ms | 3.316 | 0.525 | 0.559 | 3.309 |
| Geometry upload, decimal MB | 29.050 | 19.805 | 19.864 | 28.721 |
| Renderer draw CPU, ms | 19.726 | 13.914 | 15.238 | 19.600 |
| PSO preparation, ms | 3.415 | 3.065 | 3.083 | 3.260 |
| Texture decode, ms | 1.213 | 0.155 | 0.820 | 1.393 |
| Flush, ms | 3.243 | 2.638 | 3.025 | 3.354 |
| Fence wait, ms | 2.475 | 2.368 | 2.415 | 2.489 |
| Existing GPU timestamp scope, ms | 2.186 | 2.098 | 2.128 | 2.207 |

The replacement reduces its CPU component by about 2.75–2.79 ms and does not
increase the measured GPU timestamp scope. Every prepared draw in the two GPU
windows uses the new path, with zero preparation bypasses. The windows contain
roughly 0.97–1.00 million topology-expanded vertices per frame; raw GPU uploads
reference about 455,000 unique vertices per frame.

These are instrumented stage measurements, not ordinary FPS results. NPCs and
textures continue changing: the differing texture-decode costs show why the
larger total draw-CPU reduction cannot all be attributed to the replacement.
Draw CPU, preparation, flush and wait scopes overlap; do not sum them. The GPU
column is the existing command-list timestamp scope, not proof of complete
display latency or GPU headroom in every scene. Only the owned Haze was present
at the final process inspection; this profile has no continuous process observer
and must not be described as isolated throughout.

Private evidence: `local/gpu-pull-profile-{windows,summary,present-report}.json`,
`local/gpu-pull-profile-renderer.csv` and its `.geometry.csv` companion, run
logs/captures, and `local/gpu-vertex-pull-profile-build.log`.

## Audited camera sweep follow-up

Run `20261012-234803-235` uses the same 1440p configuration above and ordinary
candidate SHA256
`17567E736782D4F089BA17C1D7666105A67B226B954750CA845A4223EF728CF6`.
The game remains muted with real audio processing. A diagnostic at the existing
local-player mouse boundary anchors world yaw and pitch, then applies a
plus/minus 30-degree sine sweep with a four-second period. Each mode gets five
warmup seconds and 24 measured seconds (six cycles). Actual post-update game
angles, owner, phase and QPC are recorded independently of present intervals.
This is scripted camera motion with live NPC/world state, not a deterministic
whole-game replay. Normal launches do not activate this diagnostic.

An earlier alternating pad-input attempt also accumulated yaw drift. Its private
`gpu-pull-sweep` records are retained but do not establish matched-camera gains.

The first absolute-angle CPU/GPU/GPU/CPU comparison passes complete present,
mode-event, camera and sampled Haze-process coverage checks. All 16 phase bins
are covered per block; maximum actual yaw error is 1.2e-7 radians, pitch error
is zero and the motor/center remain constant. No other Haze was observed,
with observation gaps at most 2.12 seconds; other workloads are not excluded.

| Mode | Frames | FPS | Mean ms | p95 ms | p99 ms | Maximum ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| CPU | 728 | 30.40 | 32.890 | 46.341 | 50.293 | 57.719 |
| GPU | 804 | 33.52 | 29.836 | 41.672 | 46.068 | 66.999 |
| GPU | 772 | 32.21 | 31.051 | 43.241 | 49.147 | 78.870 |
| CPU | 782 | 32.62 | 30.658 | 42.910 | 45.946 | 51.307 |

Pooling all measured intervals by mode gives CPU 31.51 FPS / 31.734 ms and
GPU 32.86 FPS / 30.431 ms: about 4.3% more throughput or 1.30 ms less mean
frame time in this comparison. The block distributions overlap and GPU maximum
stalls are larger. This is modest scene-specific evidence, not proof of a
repeatable pacing fix or a universal speedup.

The reverse-order run is **not a validated comparison**: its camera CSV ends
0.329 seconds before the final measured window ends. The reporter rejects it.
The stream had been flushed only every 64 samples; a final partial batch did
not survive shutdown. The diagnostic now flushes on token changes, including
disable. That correction builds and the focused evidence tests pass; a new
game run is still required to validate the shutdown correction end to end.
The rebuilt candidate hash is
`471B8CA9FA29818516CE3D09E9978B945DF85FDB1621E8A67138729F9AB2110F`,
which must not be substituted for the measured candidate hash above.

For transparency, the rejected reverse run's untrimmed raw GPU/CPU/CPU/GPU
mean intervals are 31.706/33.340/33.151/31.138 ms, with maxima
62.897/901.738/54.890/60.184 ms. The user reported moving the game window
during this run. The acknowledgment timestamp is not the movement timestamp,
so the 901.738 ms interval is a plausible interruption, not a confirmed causal
match. It remains in the raw measurements; no convenient outlier was removed.
This interruption and incomplete audit prevent using the reverse run to
confirm tail-latency improvement.

Private evidence: `local/gpu-pull-absolute-{windows,report}.json`,
`local/gpu-pull-absolute-reverse-windows.json`, the shared presents CSV,
camera CSV and process JSONL, and `local/gpu-pull-absolute-window-move-note.json`.
The owned game closed normally with exit 0. The recording helper requires
actual-angle evidence whenever camera control is used. Focused camera and
present-mode tests pass; the camera diagnostic builds successfully.

## Remaining work

- Reference gathering and remapping still consume about 3.3 ms per frame;
  use #34 to investigate eliminating those operations and repeated uploads.
- Repeat the reverse-order absolute-angle comparison after the final audit
  flush correction. Keep interruption annotations and validate complete coverage.
- Validate more of the campaign and squad conversation and coordinate visual
  coverage with #22 before making this the automatic release path.
- Use measured remaining costs to select persistent GPU geometry (#34),
  native submission (#35), or CPU job/data replacements (#36–#38).
