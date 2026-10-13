# Bounded shader-bytecode cache bundle

Tracking: #89, with loading evidence in
[the jungle FIFO investigation](performance-jungle-fifo-stalls-2026-10-13.md).
This replaces repeated per-shader file reads on subsequent launches. It does
not persist native driver pipeline objects (#12) or replace game execution.

The renderer reads `shader_cache.bundle` from the existing shader-cache
directory near the end of successful initialization, before publishing its
ready state. A separate sorted index performs binary lookup after the existing
hot shader cache misses. It does not preload entries into the linear hot cache.
On a match, it creates the requested blob from resident bytes; missing entries
retain the existing per-file cache and compilation fallback.

The archive is capped at 64 MiB and 16,384 unique entries. Its versioned header
identifies the cache format and shader compiler contract. Validation covers the
complete canonical layout, bounded lengths, source hashes, bytecode checksums,
sorted unique records, and exact source comparison at lookup. It rejects
ambiguous duplicate payloads. Hashes detect accidental corruption; they do not
authenticate untrusted shader content.

Persistence occurs at normal window close through the explicit renderer export
in `port/main.cpp`, which matters because that path exits without SDK renderer
shutdown. SDK shutdown also invokes the export before releasing retained blobs.
Persistence merges retained shaders with the latest valid archive under a
per-cache sidecar file lock. It preserves unvisited entries and uses a flushed
temporary file followed by atomic replacement. Lock acquisition is bounded to
five seconds; failure leaves the previous archive intact. Archive maintenance
does not run during gameplay.

A first launch without an archive still uses per-file loads and builds the
archive on orderly close. It does not scan thousands of legacy files at startup.
A corrupt or incompatible archive falls back to per-file loading and is retained
for diagnosis; this prototype does not automatically replace that invalid file.
The existing launcher cache directory and cache-disable control remain in use;
there is no additional user setting.

## Validation status

The focused fixture compiles the actual SDK codec and Win32 persistence code.
It passes malformed/truncated/source/bytecode corruption, duplicate payload,
bounded merge, unique-entry/byte caps, and concurrent independent writer tests.
The concurrent test releases two processes from a gate and verifies that the
seed entry and both writers survive; corrupt prior archives remain unchanged.
Evidence: `local/shader-bundle-tests.log`.

SDK `0a796ee` passes the two-job ordinary full build. Candidate
`local/candidates/shader-bundle/HazeRecomp.exe` is 122,836,992 bytes with SHA-256
`30574D93947C1C6C9828A438EF87464CBF160CA3A51A056A94E4D7ED1431B196`.
Build log: `local/shader-bundle-build.log`. Live results are recorded below;
release promotion requires a repeatable end-to-end benefit.

No end-to-end loading or frame-pacing gain is established yet. Measurements
must include archive initialization time, fallback coverage, first resume, and
settled camera sweeps, with explicit cache and host-load conditions.

The private legacy-cache baseline contains 1,996 files / 21,220,829 bytes,
recorded in `local/shader-bundle-cache-baseline.json`. Copying and hashing this
baseline warms the OS cache, so subsequent experiments are not cold-disk tests.
No game-derived shader source or bytecode is part of this commit.

## Fresh legacy-loader baseline

`jungle-bundle-legacy-a1` uses the unchanged `vp-report-deferred` candidate
(SHA-256 `BC188C842105204458DAD1B42CA58E56A4315D73F717EF186267DFDDE0FCB21F`),
a private copy of the cache snapshot and real save, muted audio, 1440p/8192
shadows, uncapped/VSync off, and the existing fixed native geometry settings.
Navigation reaches the jungle and is visually verified before timing. Two
60-second camera windows, measurement process, and normal game close finish
successfully. No owned compilation or screenshot occurs inside the windows.

All 10,683 complete frame intervals: **89.054 FPS / 11.229 ms mean**, p95
14.271 ms, p99 16.604 ms, worst 28.331 ms; no frame exceeds 33.333 ms. The two
windows average 89.531 and 88.578 FPS. Pump means are 5.739 ms wait, 3.620 ms
FIFO, and 1.801 ms presentation. This is the old binary, not a new speedup.

Resource observations retain the failed coverage gate: 25 samples, maximum
gap 10.055 seconds against the 8-second limit. Memory is 50.65-51.44%; no
compiler is observed, but other Haze PIDs 37708 and 5880 appear. Strict isolation
fails. No improvement over previous captures can be attributed to code changes.

The slow-draw ring retains 110 events with no overwrite, all outside the two
timed windows. Across those filtered events, shader-cache loading totals
575.788 ms and driver PSO creation 229.736 ms, with zero compilation. The worst
126.113-ms draw spends 120.450 ms creating a driver PSO and 5.479 ms loading
bytecode. This warmed-cache run does not reproduce the earlier one-to-two-second
loader events. These filtered nested sums are not a complete loading budget.

Evidence: `local/jungle-bundle-legacy-a1-{comparison-evidence,draw-analysis,slow-analysis}.json`,
raw captures and resource records, run `20261013-222657-499`.

## Same-candidate absent-archive run

`jungle-bundle-training-a1`, run `20261013-223503-237`, uses the new candidate
and a fresh copy of the same legacy cache, initially without an archive. The
init log reports `absent`, 0.136 ms. The private save/settings and two 60-second
camera windows match the baseline procedure; the jungle is visually verified.
Navigation, measurement and normal game close all exit 0.

All 10,872 complete intervals: **90.622 FPS / 11.035 ms mean**, p99 16.160 ms,
worst 29.289 ms; no frame exceeds 33.333 ms. The two windows average 89.772 and
91.473 FPS. Both strict validators pass. Twenty-six resource observations
bracket the windows, maximum gap 5.340 seconds, memory 51.54-52.20%, and no
other Haze instance or compiler observed. This run changes host conditions
relative to the earlier legacy baseline, so their difference is not attributed
to the new code. The bundle is absent throughout this run.

Normal close exports exactly one successful bundle save: **230 entries,
4,920,754 bytes**, zero archive hits, 230 misses/fallbacks, and 89.460 ms save
time outside gameplay. Every archived source and bytecode payload matches its
legacy file byte-for-byte. Archive SHA-256:
`88912837ac616705f3c31ab8a98835c13f4263ab44f66286caf15e179e87da57`.
Evidence: `local/shader-bundle-training-exact-match.json`, boot log and
`local/jungle-bundle-training-a1-{comparison-evidence,draw-analysis}.json`.

## Same-candidate archive replay

`jungle-bundle-replay-b1`, run `20261013-224009-998`, copies the training cache
including its saved archive. The executable, save snapshot, settings, normal
navigation and two 60-second camera windows are unchanged. The scene is
visually verified; navigation, measurement and normal close all exit 0.

Initialization loads the 230 entries in **10.745 ms**. Final counters show
**220 archive hits and 18 legacy fallbacks**. Close saves 248 entries /
5,211,376 bytes in 94.693 ms, preserving the ten old entries not requested by
this launch. Every one of the 248 archived source/bytecode pairs matches its
legacy file exactly. This validates real archive lookup and merge behavior,
including different shader coverage between launches.

All 8,818 complete intervals: **73.501 FPS / 13.605 ms mean**, p99 20.028 ms,
worst 48.206 ms; five frames exceed 33.333 ms and none exceed 50 ms. Resource
coverage passes: 26 samples, maximum gap 5.368 seconds, memory 52.36-53.62%,
no compiler observed. Another Haze PID 12436 appears; both strict isolation
checks fail. This is not a controlled regression or speedup relative to the
isolated absent-archive run. All intervals and failed checks are retained.

The draw ring retains 17 events with no overwrite, versus 114 in the absent
run. Their filtered shader-load scopes sum to 65.193 ms, versus 549.927 ms;
the largest replay load is 8.413 ms. Zero compilation is recorded in these
retained events. These threshold-filtered sums are neither complete loading
budgets nor a measured startup-to-interactive improvement. One replay event
falls inside a timed window: 4.146 ms, of which 4.129 ms is resource resolution;
PSO lookup hits and takes 0.0028 ms. Settled pacing remains a separate problem.

The archive removes 220 per-shader load attempts in this replay for a measured
10.745-ms startup scope. That is a verified change in the I/O path. Repeatable
end-to-end loading gains, colder-cache behavior, and stable frame pacing remain
unproven; this prototype is not promoted into the user launcher.

Replay archive SHA-256:
`ef84421c54ecfb4d3e0fda2fe42d3879fafd9d5c1357cd3ae1fb63e714cd5d93`.
Evidence: `local/shader-bundle-replay-exact-match.json`, boot log and
`local/jungle-bundle-replay-b1-{comparison-evidence,draw-analysis,slow-analysis}.json`.
