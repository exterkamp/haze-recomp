# GPU occurrence experiment

Tracked in [#34](https://github.com/zdiemer/haze-recomp/issues/34), following
[GPU attribute decoding #33](https://github.com/zdiemer/haze-recomp/issues/33).
Root branch `experiment/gpu-geometry`; SDK branch `experiment/haze-gpu-geometry`.
SDK implementation commit: `6acf5d9`.

## Replacement and hypothesis

The earlier profile spent about 1.9 ms/frame deduplicating vertex references
after moving attribute decoding to the GPU. Internal GPU mode 2 skips that
deduplication. It preserves occurrence order, base indices, immutable raw
snapshots and existing topology/restart processing, allowing the GPU to process
repeated vertices. Mode 1 retains deduplication; mode 0 retains CPU decoding.
Modes 1 and 2 share shaders and pipeline state, isolating reference preparation
from shader compilation. Normal defaults are unchanged.

This trades CPU work for more vertex invocations and reference uploads. It is
not persistent geometry residency and does not remove reference gathering.
Geometry-cache mode retains its existing path. If GPU packing fails, the CPU
fetcher can consume the unremapped occurrence sequence; unsupported GPU input
formats retain CPU conversion. No new user-facing setting is introduced.

The numerical oracle now includes repeated source references with a nonzero
base and wrapped effective index arithmetic. WARP and hardware each pass
262,144 lane comparisons; benchmark mode-event tests pass. The ordinary
candidate builds with SHA256
`D16063B017DEEFCFFFD333D5E5496BE0B51E29F733E8B1B23F01CEBF1D8D0165`.
An in-game capture after selecting mode 2 shows the carrier geometry. These
checks do not prove all campaign rendering or topology equivalence.

## Measurement method

Ordinary run `20261013-000453-887` uses a private carrier save, 2560x1440 render
resolution, 1280x720 window, follow shadows, AA off, AF 1, VSync on and no frame
cap. Real audio processing is enabled and muted. Native CPU defaults are on;
geometry cache, vertex cache and vertex workers are off. Captures precede timing.

The comparison uses GPU modes 1/2/2/1 with five-second warmups and 24-second
windows. The audited camera follows a plus/minus 30-degree yaw sine sweep with
a four-second period and fixed pitch, six cycles per measured window. World
and NPC state remain live. The same executable measures both modes; a separate
instrumented build measures stage costs and must not be treated as ordinary FPS.

## Ordinary results

All four windows pass present continuity, mode events, complete camera coverage
and sampled Haze-process checks. Only the owned Haze was observed, with maximum
sample gaps below 2.12 seconds; this does not exclude other workloads or short
process lifetimes between observations. Camera yaw errors are at most 1.18e-7
radians, pitch errors are zero and every block covers all 16 phase bins.

| GPU input | Frames | FPS | Mean ms | p95 ms | p99 ms | Maximum ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Deduplicated | 750 | 31.30 | 31.954 | 44.879 | 49.470 | 58.812 |
| Occurrences | 793 | 33.09 | 30.221 | 41.086 | 44.302 | 58.004 |
| Occurrences | 792 | 33.03 | 30.277 | 41.673 | 47.633 | 68.821 |
| Deduplicated | 739 | 30.85 | 32.415 | 45.086 | 50.893 | 63.918 |

This first sweep shows about 6% higher throughput with occurrences, with lower
mean, p95 and p99 in both occurrence windows than either deduplicated window.
Maximum stalls are not consistently better. Do not multiply this scene-specific
result by earlier experiments' percentages or claim a stable frame rate.

Normal close exits 0. The final renderer log has no dropped draws, texture
decode failures or GPU preparation bypasses. Its cumulative GPU totals are
7,979,760 draws, 4,101,145,874 vertex references and 125,250,870,848 uploaded
bytes across both modes, including loading and menus; these are not per-frame
gameplay costs. Private records are `local/gpu-occurrences-motion-*` and the
run logs/captures. Numeric oracle output is `local/gpu-occurrences-oracle.log`.

## Separate stage profile

Instrumented candidate SHA256
`63DBCDCC6974EEDD7E7298C7789F5B3FF3616B796F9C6DB42F5394572CBF173D`
uses the same implementation. Run `20261013-000933-088` follows the same
camera path and configuration with 1/2/2/1 ordering and 24-second windows.
Present continuity, mode, actual-angle and sampled process checks all pass.
The stage records contain 722/755/739/669 consecutive intervals. All prepared
draws use the GPU path, with zero preparation bypasses and no vertex/fragment
shader compilation. The first two windows have four/two PSO misses; creation
cost is 0.0223/0.00025 ms per frame, respectively, and zero in the last two.

| Scope, per frame | Dedup first | Occurrence first | Occurrence second | Dedup second |
| --- | ---: | ---: | ---: | ---: |
| Vertex preparation, ms | 4.141 | 2.423 | 2.402 | 4.217 |
| Reference gathering, ms | 1.387 | 1.353 | 1.332 | 1.397 |
| Remapping scope, ms | 1.801 | 0.024 | 0.025 | 1.816 |
| Fetch plan, ms | 0.255 | 0.315 | 0.315 | 0.260 |
| Raw snapshot, ms | 0.530 | 0.561 | 0.561 | 0.579 |
| Topology indices, ms | 0.192 | 0.027 | 0.023 | 0.195 |
| Uploaded references | 452,218 | 938,901 | 925,060 | 434,927 |
| Geometry upload, decimal MB | 19.468 | 23.232 | 22.690 | 18.906 |
| Renderer draw CPU, ms | 15.472 | 13.365 | 13.321 | 15.422 |
| PSO preparation, ms | 2.986 | 3.001 | 2.968 | 2.935 |
| PSO lookup, ms | 0.111 | 0.105 | 0.103 | 0.103 |
| Vertex program preparation, ms | 1.341 | 1.353 | 1.346 | 1.345 |
| Fragment program preparation, ms | 1.320 | 1.333 | 1.297 | 1.312 |
| Texture decode, ms | 1.258 | 1.118 | 1.012 | 1.194 |
| Flush, ms | 3.301 | 3.249 | 3.248 | 3.258 |
| Fence wait, ms | 2.522 | 2.553 | 2.567 | 2.465 |
| Existing GPU timestamp scope, ms | 2.183 | 2.209 | 2.214 | 2.100 |

Skipping deduplication removes about 1.78 ms from its measured CPU scope;
total vertex preparation improves about 1.7-1.8 ms. The remaining remapping
scope includes timer/branch overhead even though the remapper is not called.
The GPU handles about twice the references and uploads about 4 MB more geometry
per frame, with a small increase in the existing GPU timing scope. This supports
the tested tradeoff. Those GPU timestamps cover command-list work, not the
complete display path or all possible scenes.

Reference gathering remains about 1.3-1.4 ms. PSO preparation remains about
3 ms, while actual lookup is only about 0.1 ms; repeated program preparation
is a better lead than replacing the hash table blindly. Vertex-program timers
also include preparation outside get_pso. These scopes overlap with each other,
draw CPU, flush and waits, and must not be summed into a whole-frame budget.
Live texture/NPC variation also prevents attributing the entire draw-CPU delta
to deduplication alone. Normal profile close exits 0.

Private evidence: `local/gpu-occurrences-profile-{windows,summary,present-report}.json`,
renderer CSV and `.geometry.csv`, camera CSV, process observations and run logs.

## Reverse-order ordinary confirmation

Run `20261013-001552-315` uses the same ordinary executable hash and settings,
with mode order 2/1/1/2, five-second warmups and 24-second camera sweeps.
The scene was verified before timing. All four windows pass present continuity,
mode events, camera trajectory and sampled Haze-process checks. Maximum actual
yaw error is 1.19e-7 radians, pitch error is zero, and the largest process
observation gap is 2.13 seconds. Only the owned Haze was observed.

| GPU input | Frames | FPS | Mean ms | p95 ms | p99 ms | Maximum ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Occurrences | 758 | 31.61 | 31.632 | 42.662 | 46.824 | 50.318 |
| Deduplicated | 739 | 30.82 | 32.447 | 45.058 | 50.063 | 57.517 |
| Deduplicated | 753 | 31.42 | 31.830 | 44.198 | 52.408 | 64.621 |
| Occurrences | 794 | 33.10 | 30.211 | 40.866 | 44.933 | 53.621 |

Both occurrence windows improve mean, p95 and p99 relative to either
deduplicated window in reverse order too. The gain varies and neither mode
achieves consistent pacing. Normal close exits 0; the final log has zero
dropped draws, texture decode failures and GPU preparation bypasses. Private
records use the prefix `local/gpu-occurrences-reverse-` plus the run captures/logs.

## Remaining work

- Validate the squad conversation and additional campaign scenes before
  automatic promotion; both ordinary camera comparison orders now pass.
- Check rendering equivalence with broader topology and deforming-model
  coverage; the numeric decoder oracle is not a whole-renderer proof.
- Investigate direct index/reference submission and persistent raw geometry
  under #34, including ownership, invalidation and resource lifetime.
- Use #35/#12 to investigate repeated shader/pipeline preparation, based on
  the measured preparation-versus-lookup costs above.

The experiment remains off by default; stable pacing and 144 FPS are unproven.

## Native index-reference preparation follow-up

The measured reference-gathering scope still costs about 1.3 ms/frame after
CPU deduplication is removed. A follow-up under #34 batches contiguous big-endian
16/32-bit index decoding into native reference records with SSE2, retaining a
portable scalar implementation. The batch keeps the base index separate and
preserves occurrence order. It only publishes its reference count after all
indices pass restart-marker detection; a marker returns to the existing scalar
cut builder, overwriting any speculative partial output. Noncontiguous or
wrapped source spans retain the existing resolver path.

An internal CPU-work bit 8192 selects this comparison (5379 versus 13571 with
the current GPU-occurrence baseline). The default remains off pending in-game
measurements; no user-facing graphics option is added. The numerical/bounds
fixture passes 8,580 cases in each SIMD and forced-scalar build, including all
source alignments, output alignment offsets, short tails, every restart lane,
nontruncated 32-bit restart values on 16-bit indices, high base indices, empty
input and protected pages immediately after the source and destination spans.
These tests do not establish an FPS improvement; integrated profiling and
ordinary camera comparisons remain required.

SDK implementation: `ca743b0`. The integrated ordinary build and full native
regression suite pass, including the existing 262,144-lane GPU decoder oracle.
Candidate `local/candidates/native-index-refs/HazeRecomp.exe` has SHA256
`54802A22F6528D704FE5308916C7BFC44FBECE4BD049DFCEEC394C28CB3D7F07`.
