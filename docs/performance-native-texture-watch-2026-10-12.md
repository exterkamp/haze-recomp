# Native texture write tracking in Haze

Experimental branches `experiment/native-cpu-kernels` and
`experiment/haze-native-kernels` now support regional Windows write tracking
for texture mutation detection. It remains off by default. The prior
[feasibility probe](performance-write-watch-2026-10-12.md) ruled out whole-arena
resets; this implementation queries/resets only a bound texture's pages.

## Integration and invalidation

`HAZE_TEXTURE_WRITE_WATCH=1` opts Haze's demand-committed reservation into native
write tracking and explicitly registers it with the renderer. The reservation
covers the original 4 GB plus 64 KB guard. Registration/allocation failure
retains the full-hash path; other applications and SDK consumers are unchanged.
Renderer CPU-work bit 4096 selects reuse: 1283 is the existing renderer baseline,
5379 adds only native texture tracking. Benchmark selectors now accept 0..8191.

The native helper owns monotonic per-page generations. Each regional query
captures/resets dirty pages together, advances their generation, and computes
the maximum generation over the complete texture source. A texture can reuse
its existing content identity only when that stamp and hash algorithm match
its previous validated baseline. Shared-page generations invalidate overlapping
textures even when a different texture already reset the page's tracking.
Writes after a query remain tracked for later bindings/frames. The renderer is
the sole query/reset owner; producers continue ordinary host writes.

Creation/eviction clears the stamp. Mode changes and hash-algorithm transitions
require a full read. An unreadable source or failed refresh never marks stale
GPU contents as current. Query failure, invalid returned pages or generation
overflow disable reuse. Out-of-arena ranges use full reads. The registered
reservation must not be decommitted/reset or have write tracking reset by
another owner; the current Haze arena retains committed pages for its lifetime.

`RSX_TEXTURE_WRITE_WATCH_VALIDATE=1` checks every proposed skip with a complete
hash. It queries again after the read: intervening writes invalidate the stamp
for the next check; a changed hash with an unchanged stamp disables reuse and
logs the mismatch. Validation mode intentionally retains full-read overhead.
`LD_TEX_RACE`/`LD_FBDBG` retain their original full-read diagnostics.

The light profiling CSV adds native query time, queries, dirty pages, skipped
bytes, proposed skips, audits, unstable audits and failures. Normal window close
also prints a tracking summary. Present CSV mode markers retain independent
settled-window validation.

## Correctness evidence

The full build/regression suite passed in
`local/native-texture-watch-build-tests.log`. The standalone oracle now uses
the exact production regional helper. Both whole-arena control and native
regional modes pass 4,000 random mutation/binding operations and 128 independent
producer-thread writes each, including shared pages, late writes, unaligned
tails and demand commitment near the reservation's end. Additional tests cover
actual API failure on an untracked reservation and generation overflow.

The live-cache test directly checks audited reuse, shared-page changes,
hash-algorithm transitions, disabled-mode fallback and repeated failed-refresh
retries. Its failed-refresh fixture makes queries/hashes readable and the
subsequent decode unreadable, verifying that a later frame retries the decode
even when the native page stamp is unchanged. Existing GPU upload/readback,
vertex, memory, SPU, scheduling, input, audio and graphics tests also pass.
Benchmark reporter and priority tests pass with the extended mode range.

Muted full-hash audit run `20261012-193704-612` used a fresh private carrier
checkpoint/controller, 2560x1440, AA off, AF1, follow-resolution shadows, VSync
on and no host cap. Native cursor/FIFO/PPU-queue/SPU-idle paths stayed enabled.
Viewed captures 008271/011240 verify the initial carrier and movement to the
squad. Across the complete flushed renderer CSV, **1,208,165 proposed skips**
were checked with **zero unstable audits and zero mismatches/query failures**.
This covers that route, not all levels or arbitrary concurrent snapshots.

A settled 360-frame stationary audit block (9778–10137) averages 371.34 checked
skips, 107.13 MB of eligible skipped bytes, 601.11 dirty pages and 0.723 ms of
query overhead per frame. Queries average 774.57/frame because validation
performs a second query on each proposed skip. Full hashes still read
112.30 MB/frame and cost 3.638 ms. Audit FPS is not optimization evidence.
Normal shutdown returned zero.

The full-suite audit candidate SHA256 is
`FF3EBB8EDDC23CC98E36B0C0A3DF8D70A79F16B6F2F09ADD3E89AA330F639841`.
A subsequent diagnostic-only change prints the summary on the host's actual
normal window-close path. Final profiling candidate SHA256:
`8D3F4B6A161873EB38362851D65D398B0F4026692ED06B9015A24337DA6E2683`.
Ordinary candidate `local/candidates/native-texture-watch/HazeRecomp.exe` SHA256:
`1EAE31BA9A7FC9290638E92279FA7177A2EF2B8810D7FFF686D6080A57F4A1A8`.

## Renderer-stage comparison

Muted light-profile run `20261012-194230-624` uses the same checkpoint/settings,
with validation disabled and a tracked reservation in both modes. Four
180-frame blocks follow 80-frame warmups, changing only bit 4096. Every row's
mode marker is checked. Captures 008392/010718 were viewed before/after; animated
NPCs and aircraft change the workload despite the fixed camera. Normal close
returned zero and reported healthy tracking with no failures.

| Mode | Frames | Diagnostic FPS | p95 ms | Hash ms | Query ms | Draw CPU ms |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| Full read | 9663–9842 | 25.707 | 52.996 | 3.673 | 0.000 | 21.973 |
| Native | 9981–10160 | 25.975 | 53.909 | 0.140 | 0.496 | 20.705 |
| Native | 10249–10428 | 23.765 | 59.162 | 0.141 | 0.502 | 21.952 |
| Full read | 10521–10700 | 23.994 | 57.032 | 3.648 | 0.000 | 24.475 |

Native mode skips 108.33/106.98 MB per measured frame. Mean hashing plus native
queries falls from 3.660 to 0.639 ms, approximately **3.02 ms less renderer work**.
Overall diagnostic FPS/pacing is mixed. The scopes are overlapping elapsed
times, include preemption, and cannot establish total-frame or producer CPU
savings by themselves. Ordinary-build comparisons must include those costs.

Native-mode renderer subscopes average 7.638/7.973 ms for vertex packing,
3.214/3.383 ms for pipeline preparation and 1.707/1.871 ms for texture decode.
Vertex packing includes reference gathering (1.558/1.620 ms), remapping
(2.019/2.102 ms), planning (0.295/0.308 ms) and decoding (3.639/3.808 ms).
Pipeline preparation includes vertex/fragment program preparation; no shader
compilation or PSO creation occurred in these measured windows. GPU timestamps
average 2.078/2.205 ms. Flush scopes average 3.371/3.590 ms, including
2.373/2.533 ms of fence waits; some flushes also overlap draw processing.
These are elapsed scopes, not an additive partition or CPU utilization shares.

For an additive chronological view of those diagnostic native frames, valid
boundaries put 10.191/11.781 ms before the first draw, 27.130/29.153 ms from
first to last draw, and 1.177/1.275 ms after the last draw. The draw span includes
command-stream gaps as well as the timed draw scopes. Boundary arithmetic uses
180/178 frames; the two missing/invalid boundaries in the second block remain
included in the full 180-frame stage and interval statistics. These diagnostic
intervals average 38.499/42.079 ms, distinct from the roughly 35 ms measured in
the ordinary build. They do not prove how much ordinary-build time belongs to
each producer job, command wait, scheduling delay or presentation operation.

## Ordinary-build comparison

Muted ordinary run `20261012-194600-168` uses the final nonprofiling candidate,
a fresh private carrier checkpoint/controller and the same graphics/native
settings. Validation is disabled. Both modes retain the tracked reservation;
only renderer bit 4096 changes. Four 45-second blocks follow five-second
warmups. Viewed captures 008379/015260 verify the carrier before/after;
animated NPCs and aircraft change the workload over time. Normal close returned
zero; tracking stayed healthy with zero failures.

| Mode | Frames | FPS | Mean ms | p95 ms | p99 ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| Full read | 1105 | 24.574 | 40.694 | 57.092 | 68.998 |
| Native | 1301 | 28.951 | 34.541 | 50.176 | 56.841 |
| Native | 1257 | 27.937 | 35.795 | 50.326 | 56.368 |
| Full read | 1126 | 25.048 | 39.924 | 56.427 | 61.391 |

Throughput from mean block frame time rises from 24.808 to 28.435 FPS,
approximately 14.6%;
mean block p95 falls from 56.759 to 50.251 ms. Both native blocks beat both
full-read blocks on throughput and p95. The present reporter verifies complete,
consecutive intervals and settled mode markers. Process observations bracket
each block, with maximum gaps of 2.192/2.122/2.113/2.125 seconds; only the owned
Haze was observed. Other applications and lifetimes between samples are not
excluded. This is a useful scene-specific gain, not consistent 30 FPS or a
claim that the squad-conversation slowdown is fixed.

Whole-process CPU averages 4.09/4.39/4.38/4.39 cores across these blocks.
More frames per second can raise aggregate CPU utilization while lowering CPU
cost per presented frame. These totals do not identify critical-path costs.
A separate launch comparison against an ordinary untracked reservation and
broader level/producer-write coverage remain necessary before enabling native
tracking by default. Reversing the block order checks one source of scene drift.

### Reverse order under concurrent Haze load

Run `20261012-195254-705` reverses the modes with the same ordinary executable,
settings, fresh private fixture and 45-second/five-second measurement schedule.
Captures 008457/019048 were viewed and verify the stationary carrier. Normal
close returned zero; write tracking remained healthy with zero failures.

| Mode | Frames | FPS | Mean ms | p95 ms | p99 ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| Native | 1020 | 22.687 | 44.078 | 61.598 | 71.214 |
| Full read | 924 | 20.564 | 48.629 | 68.252 | 77.282 |
| Full read | 837 | 18.624 | 53.694 | 77.699 | 91.430 |
| Native | 795 | 17.709 | 56.470 | 78.832 | 102.494 |

The present reporter verifies all complete consecutive intervals and mode
markers, but its process-observation check rejects treating this as a clean
comparison: PID 10260 from `.worktrees/shadow-lighting`, candidate
`visibility-decision-trace`, overlaps every measured block. The separate
contended report preserves those observations. Throughput falls throughout the
run, including between the two identical full-read blocks, and the last native
block loses. This run does not confirm or refute the first run's gain in a
stable workload. No other agent's process was changed or stopped.

The first run therefore establishes a scene-specific repeated gain; broader
repeatability and consistent frame pacing remain open. The experimental path
stays off by default.

## Next CPU preparation lead

The current optional `RSX_VERTEX_CACHE` caches decoded CPU payloads only after
reference gathering, remapping and plan preparation. A hit still hashes and
compares reference/source bytes and copies the payload; the draw then copies
it into a transient GPU ring and emits topology indices. It cannot eliminate
the entire approximately 7.8-ms preparation stage. A broader geometry cache
would need complete draw/layout/source identity and shared native page
generations, preserve inline/default/frequency/restart behavior, and retain
buffers until all in-flight GPU users finish. Measure unchanged-source reuse,
eligibility and invalidation cost before claiming a gain or choosing its size.

Artifacts: `local/native-texture-watch-audit-{run,blocks,summary}.json`, its
renderer CSV; `local/native-texture-watch-stage-{run,blocks,summary,boundaries}.json`,
renderer CSV and process observations; `local/native-texture-watch-{run,windows,report}.json`,
present CSV and process observations. No game-derived captures, saves,
translations or binaries are committed.
