# Reuse the vertex hash within one pipeline lookup

SDK commit `b18f371` removes a duplicate vertex-program hash from `get_pso`.
This is independent of the optional cross-draw VP metadata cache, which remains
opt-in after its mixed FPS comparisons. The change retains one lookup's exact
FNV result in a local variable and uses it for both the pipeline-key prefix and
current pipeline metadata. It does not cache across draws, skip byte validation,
change hash algorithms, or change the pipeline identity.

## Profiling evidence

The native-FIFO-on carrier diagnostic `20261012-182705-707`, ordinary binary
`DB142083...`, selected main TID 59352 and renderer TID 35580 for 12 seconds.
It captured depth-20 stacks every third round at randomized seven-ms intervals.
See [the native FIFO report](performance-native-fifo-wake-2026-10-12.md) for CPU
counters, suspension overhead and limitations. This was separate from the later
conversation FPS windows.

Renderer instruction samples include 76 in `fnv1a`. Unwound FNV call sites
include eight samples returning to `get_pso+0x5A9` and eleven returning to
`get_pso+0x9AD`. Instruction/stack counts include waits and are not CPU shares.
Source inspection found two calls to `ld_vp_hash` over the same renderer-owned
VP bytes in one `get_pso` invocation, when CPU-work bit 512 is off. No command
dispatch occurs between those calls. The second result populated metadata.

The matching pre-change `rsx_live_draw.c.obj` disassembly confirms both sampled
return addresses follow real FNV calls with the same program pointer and seed.
The first supplies the pipeline prefix; the second supplies the metadata.
This establishes executed duplicate work, rather than relying on source alone.

## Implementation and validation

`const u64 vp_hash` now holds the first result. The pipeline key is initialized
from it, and metadata later receives the same local value. Cached VP mode keeps
using its existing `vp_info.hash`. All intervening pipeline-state key additions,
fragment handling, diagnostic variant tracing and rejection paths are retained.

The full `scripts/build.ps1 -SpuFma -Test` suite passed, including actual graphics
pixel readbacks, shader/render-state checks, vertex conversion, native cursor
oracles and both FIFO wait modes. Build/test log:
`local/native-vp-hash-reuse-build-tests.log`.

Matching optimized object disassemblies show 17 FNV call relocations in the old
`get_pso` and 16 after the change. The redundant program-byte walk is removed
from the generated machine code. This does not quantify end-to-end FPS gains.

Candidate: `local/candidates/native-vp-hash-reuse/HazeRecomp.exe`, SHA256
`D9CFBBC4C04BC24DE1FA35A895E282A1679188999753F918A25EAA1158D5E140`.

Local artifacts: `native-fifo-active-{rips,stacks}.json`,
`native-fifo-get-pso-before.asm`, `native-vp-hash-get-pso-after.asm`. Raw game
captures and binary/PDB artifacts remain ignored. Stable carrier frame pacing
and broad unlocked gameplay correctness remain unproven.

## In-game post-change diagnostic

Muted run `20261012-183812-351` loads a fresh private copy of the carrier
checkpoint using this candidate, native cursor/queue/idle/FIFO paths enabled,
2560x1440, VSync on and host cap zero. Capture 010193 verifies gameplay. Another
agent's Haze was present at the separate thread-discovery sample; it was left
running. This profile cannot establish an isolated speedup against the earlier
run.

The subsequent 12-second profile selected main TID 55716 and renderer TID
10708 with the same depth/cadence as the earlier diagnostic. It suspended those
threads 2,736 times for a total 987.74 ms, maximum 17.95 ms. Main CPU was
7,062.5 ms and renderer 6,343.75 ms; differences across these live scenes are
not controlled optimization measurements. The renderer still shows texture
mutation hashing, vertex fetch/remapping and FIFO processing. Of its unwound
FNV callers, seven return to the remaining VP-hash site `get_pso+0x657`, which
the matching optimized object confirms. The removed second VP walk is absent
from that object, independently of sampling coverage.

Artifacts: `local/native-vp-hash-{run,active-rips,active-stacks}.json`. Sampling
was completed before the separate ordinary timing window.

The subsequent 45-second stationary carrier window (five-second warmup, no
sampling/captures/input/builds inside) retained 1,012 consecutive present
intervals. It measured 22.51 FPS, mean 44.43 ms, p95 62.49 ms, p99 69.07 ms and
maximum 106.35 ms. Native FIFO mode settled on main TID 55716 before the window.
All 22 in-window process observations also contain the shadow-lighting agent's
Haze PID 32184, so this is a contended current-build baseline, not an isolated
before/after estimate for hash reuse. The renderer's final counters report zero
fetch, primitive, allocation, PSO, ring and surface drops. Capture 013161 verifies
the carrier after the window. Normal shutdown returned zero and flushed the CSV.

Artifacts: `local/native-vp-hash-{windows.json,presents.csv,processes.jsonl}` and
`local/native-vp-hash-contended-report.json`. The saved results confirm that the
candidate remains playable at this checkpoint, while substantial pacing work
is still required. No whole-game percentage gain is attributed to this change.
