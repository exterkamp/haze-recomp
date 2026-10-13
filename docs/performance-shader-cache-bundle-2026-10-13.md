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
Build log: `local/shader-bundle-build.log`. Live persistence/reload validation
is still required before performance conclusions or release promotion.

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
