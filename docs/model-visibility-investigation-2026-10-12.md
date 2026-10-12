# Nearby model visibility investigation (in progress)

The user reports entire soldiers and airships disappearing and reappearing,
especially up close, including while neither the player nor camera moves.
The requested investigation is on a new branch in the existing worktree:
`investigate/model-visibility`, based on `9c48009`. Its independent SDK branch
is `investigate/haze-model-visibility`, based on `9058dc4`. This preserves the
previous shadow clear correction. No model visibility fix is established yet.

## Initial native evidence

Run `20261012-072933-476` uses the preserved executable
`build/haze/HazeRecomp.exe` (SHA256
`EF9CF6AB69EB984E6F22805C003608220CD45E78ECA126B2C5BDAFF7F7B06B7B`).
It runs at native scene/shadow resolution, in a 1280x720 window, with the
30 FPS ceiling. Window-position correction and the Z16 clear correction are
on; shadow comparison and fragment depth export retain their off defaults.
The save is copied into a dedicated `local/state/visibility` directory.

Three requested-frame bursts were captured:

| Input-neutral capture | Guest frames | Frames |
| --- | --- | ---: |
| Opening ramp | 3218 through 3337 | 120 |
| Near the squad and parked airship | 5040 through 5279 | 240 |
| After moving into the squad | 6157 through 6396 | 240 |

The opening and near-squad contact sheets show opaque soldiers and an opaque
parked airship. This is a successful baseline, not reproduction of the user's
reported defect. Contact sheets sample frames and cannot rule out a missing
model between samples. Adjacent-frame difference ranking was therefore added
locally to locate abrupt changes for full-size inspection. The largest change
in the near-squad burst (5155 to 5156) retains all six nearby soldiers and the
airship; camera sway and animation change the image.

The third burst triggers the objective and a scripted aircraft flyover.
Despite neutral player input, its camera changes views. Its large differences
cannot be classified as disappearing geometry without accounting for those
camera cuts. It is not a stationary-camera comparison.

The gated draw CSV covers parts of 122 frames (3217 through 3338), with
174,080 rows classified `execute` and no recorded draw-drop outcomes. These
rows establish submission, not visible rasterized pixels, and the gate's first
and last frames are partial. The observed depth functions in these rows are
LEQUAL and ALWAYS, rather than EQUAL. Runtime report logging includes positive
ZPASS results (`65535`) for indices 3, 4, and 5; early occlusion-query returns
of zero do not explain this particular captured window. These samples cannot
rule out later query or geometry corruption.

This run reached the ten-minute runner timeout; its successful-present ring
was not flushed. It supplies visual and draw-stream evidence, not frame-pacing
results or a normal-exit validation. The raw game-derived captures and CSV
remain local and ignored.

## Automatic-resolution evidence

Run `20261012-074208-886` uses the same executable with automatic render
resolution, resolved by the boot log to 2560x1440, and native shadows. It
starts the chapter from the beginning. A consecutive 90-frame interior squad
burst covers frames 7178 through 7267. Inspected contact-sheet samples retain
opaque close soldiers. The gated draw CSV contains 95,744 `execute` rows over
parts of 91 frames (7178 through 7268), without a recorded drop outcome.
This run also timed out, so no successful-present CSV was flushed; mode
control files were not connected for this run. Its settings are the executable
defaults and boot-log graphics settings, rather than later control-file values.

The main checkout's default executable differs from the tested executable:
SHA256 `971EB2469158EB1A5EA70F8D7CAA870ECD447E55CB327559B190F2737C7B523A`.
Its embedded strings include the window-position and depth-export controls but
omit `RSX_DEPTH_CLEAR_FILE`. It predates the Z16 clear correction. This is a
build difference to test, not proof of what the user ran or of a visibility
cause. The main SDK branch's later queue work does not change renderer code.

## Live clear and vertex-processing comparison

The native-size baseline has not reproduced the complete disappearance.
Run `20261012-075430-264` connects live mode controls at 2560x1440, with
native shadows and the other baseline modes preserved. It exited normally
with code 0, preserving 5,942 successful presents. Every captured frame in
the following bursts has a corresponding successful-present row:

| Burst | Guest frames | Depth clear | CPU work flags |
| --- | --- | ---: | ---: |
| Near squad, legacy | 4368–4487 | 0 | 1283 |
| Near squad, corrected | 4587–4706 | 1 | 1283 |
| Closer, corrected | 5348–5437 | 1 | 1283 |
| Closer, legacy | 5479–5568 | 0 | 1283 |
| Closer, reference vertex path | 5881–5939 | 1 | 0 |

These are consecutive input-neutral captures. Shadow comparison/depth export
are 0 and window-position correction is 1 throughout. Frame 5880 is excluded
from the reference comparison because the present-boundary mode switch had
not yet applied. Two early menu/loading screenshot labels read a stale clear
control file; the actual present CSV records clear mode 0 for those frames.

Contact sheets retain the squad and parked airship in both clear modes and
the reference vertex path. The four largest adjacent differences in each of
the first four bursts were inspected as paired images: soldiers and the
parked aircraft remain visible. Moving soldiers, aircraft flyovers, foliage,
and bright deck patches account for those image changes. This does not rule
out a disappearance outside the inspected pairs. The gated closer/corrected
CSV contains 120,320 `execute` rows over parts of 91 frames (5348–5438), no
recorded drops, with LEQUAL and ALWAYS depth functions. CSV `#blit`/surface
comment records are excluded from the draw counts. D3D debug was disabled
for this run, so this is not a GPU debug-layer validation.

## Older executable comparison

Run `20261012-080232-010` tests the main checkout's older default executable
directly, using the same isolated save, input file, and graphics settings.
It exited normally with code 0 and preserved 7,736 successful presents. The
near-squad burst covers consecutive frames 5030–5149 and the closer burst
5255–5344. Their inspected contact sheets and four largest difference pairs
retain the soldiers and parked aircraft. A post-flyover burst covers
7613–7732; three nearby soldiers remain visible in its inspected samples
and four largest difference pairs. The scripted camera has changed angle
between the closer and post-flyover windows; these are not matched viewpoints.
The closer draw trace contains 129,024 executed draws over parts of 91 frames
(5255–5345), without a recorded drop outcome. All 330 captured burst frames
have corresponding successful-present rows with CPU flags 1283, shadow
comparison/depth export 0, and window-position mode 1. This older executable
does not expose or record a depth-clear mode. The generic screenshot helper's
clear labels are therefore inapplicable to it.

No evidence yet establishes the older clear behavior as the visibility cause.

## Follow-resolution shadow comparison

Run `20261012-081149-075` tests the corrected worktree executable at 1440p
with follow-resolution shadows (5120x5120), covering the larger surface size
used by older default configurations and the main checkout's profiling runs.
It exited normally with code 0 and saved 6,987 successful presents. All four
90-frame bursts are consecutive, have successful presentation records and
contain no mode transition:

| Burst | Capture/draw counters | Depth clear |
| --- | --- | ---: |
| Near squad, corrected | 5208 through 5297 | 1 |
| Near squad, legacy | 5396 through 5485 | 0 |
| Closer, legacy | 6750 through 6839 | 0 |
| Closer, corrected | 6895 through 6984 | 1 |

CPU flags remain 1283, shadow comparison/depth export 0, window-position mode
1. The inspected contact sheets and four largest difference pairs in each
burst retain the squad and parked aircraft. In the closer legacy burst, a
soldier walks very close across the foreground and remains opaque in the
inspected pairs. The draw trace contains 138,752 `execute` rows over parts
of 93 frames (6748 through 6840), without a recorded drop outcome. GPU debug
was disabled. These comparisons have not reproduced a complete disappearance.

## Draw comparison and capture alignment

`scripts/compare_draw_frames.py` compares submission counts grouped by render
target, primitive, source vertex count, actual pipeline and fragment program.
It excludes the CSV's comment records and reports draw outcomes/depth functions
for both selected frames. Groups are not model IDs: one model may use multiple
groups and different models may share a group. A changed group count alone is
not evidence of a missing model.

In the normal live 3D path, the requested screenshot and draw CSV both use
counter N. Readback occurs before presentation; the subsequent successful
present row uses N+1. Its mode stamp is recorded after live-control polling.
The validators check both adjacent present records and exclude transitions
(including reference-path capture 5880), rather than assigning mixed work to
one condition. The numbers above retain their original capture/draw counters.

For example, the opaque closer pair 5392/5393 has 1,363/3,039 executed rows;
its 503 changed submission groups do not establish a visibility failure.
The opaque older-executable pair 5272/5273 has 1,361/1,387 executed rows and
32 changed groups. These are evidence that total submission counts alone
cannot classify the reported visual defect.

```powershell
python scripts/compare_draw_frames.py local/visibility-main-old-draws.csv --frames 5272 5273 --output local/visibility-old-draw-comparison.json
```

The utility was exercised against both native draw traces. Raw GPU readbacks
and gated CSV logging perturb timing; these capture intervals are not valid
performance measurements. No renderer defaults or model-visibility behavior
were changed by this investigation.

## External window capture comparison

Two further runs test the concern that native readbacks and per-draw logging
mask a timing-sensitive failure. Both use the preserved executable at 1440p
with native shadows. An experimental local recorder targets the test process's
Haze window using `PrintWindow`, writes an MJPEG video, and records host QPC
timestamps. This avoids the game's native screenshot path, but Windows message
handling and video encoding still add overhead. Screen samples can repeat,
skip presentations, or fail to contain valid game pixels. They are not guest
frame IDs or performance measurements.

Run `20261012-083842-069` disables native capture and draw CSV logging. It exits
normally with code 0 and saves 5,570 successful presents. An attempted nearby
mechanic recording contains 1,351 screen samples. Its first 276 samples show
the scene; the remaining 1,075 decode as a near-black image. The transition
is bracketed by present timestamps around counters 4925 through 4927. The
renderer also changes from roughly 880 executed groups per frame to roughly
70, measured from its cumulative counters. This cannot be dismissed solely
as a Windows capture failure, but no native image of the black state exists.
The camera was still moving during the early recording because a queued stick
command had not finished. This is neither a stationary comparison nor a
confirmed model-specific disappearance/reappearance. The whole-screen black
transition remains an unresolved observation.

Run `20261012-084835-356` repeats the fixed pad-poll route, with native capture
available but unused until after the external recordings. The route reaches
a different position and camera angle, despite matching poll counts. Pad polls
do not reliably specify movement distance or rotation; future reproduction
must verify the view and input completion rather than reuse waits alone.

The repeat run exits normally with code 0 and saves 8,332 successful presents.
All present rows retain CPU flags 1283, depth clear 1, window position 1,
shadow comparison 0 and depth export 0. Draw logging remains gated off.

| External recording | Samples reviewed after input completion | Elapsed time | Largest capture gap |
| --- | ---: | ---: | ---: |
| Near squad, samples 360 through 2999 | 2,640 | 86.43 s | 1.65 s across the full recording |
| Closer squad, samples 0 through 1349 | 1,350 | 44.98 s | 0.103 s |

Each selected sample's QPC timestamp has both a preceding and following
successful-present record. Contact sheets and the eight largest adjacent
changes within the selected soldier regions were inspected for each recording.
They retain opaque soldiers and the parked aircraft; animation, flyovers,
foliage and bright deck patches account for the inspected changes. These
samples do not rule out a missing model in an uninspected or uncaptured image.
Native capture `requested_007984.ppm`, taken after both recordings, also shows
the opaque nearby squad and aircraft. The whole-screen black transition did
not recur in the inspected repeat-run evidence. No visibility fix is established.

Raw videos, screenshots, timestamps, validators and experimental capture
helpers remain ignored in `local/`; they contain game-derived pixels. The
correlations are saved in `visibility-external-correlation.json` and
`visibility-probe-validation.json` alongside the local recording metadata.

## Scoped source findings

The SDK source below is from the investigation's preserved `9058dc4` base.
These checks narrow hypotheses; none establishes the reported failure.

- **Occlusion reports:** `libs/video/cellGcmSys.c:2054` handles GET_REPORT
  by storing `65535` for type 1 and zero for other report types. The scalar
  `cellGcmGetReport` at line 2999 reads that stored value. The actual report
  capacity is 65,536 slots, so observed indices above 255 are valid despite
  an older comment describing a smaller array. In the local lift, the
  imported scalar stub `00C2790C` has seven direct call sites: six in
  `00733F30` read non-type-1 reports; the remaining call in `0073B2B8` reads
  type 1. The latter has one direct caller, with return address `00663D84`.
  That caller divides the sample count by a projected rectangle's area and
  uses the result to scale a subsequent value. This suggests an area-based
  opacity calculation, rather than per-model submission culling. The function
  has not been identified by name, and indirect or guest-memory consumers
  are not ruled out. No report behavior was changed.
- **Display selection:** `libs/video/rsx_live_draw.c:3530` associates a
  display buffer with its resolved render surface. Presentation first tries
  an exact registered surface, then that resolve association, then a logged
  current-surface fallback. The first five investigation runs' `boot.stderr.log`
  files contain no scanout-fallback or no-color-surface present-skip messages.
  This does not prove every selected surface is correct.
- **GPU upload lifetime:** `ld_flush` in `rsx_live_draw.c:1922` submits the
  command list, signals a fence and waits for completion before resetting
  the allocator. The vertex/index capacity branch at line 7373 calls this
  before rewinding transient ring cursors. There is no demonstrated overwrite
  of an in-flight vertex upload in this path. Those five stderr logs also lack
  the renderer's `[d3d-fail]` messages; only the first run enabled D3D debug.
- **SPU terminology:** `SPU_DEPTH_RING_FILE` and `spu_depth_guard` in
  `runtime/spu/spu_drain.c:343` concern host call recursion and diagnostic PC
  history, not graphics depth or occlusion history. None of those five stderr
  logs contains the `[spu-depth]` recursion-limit halt message. These controls
  are not a graphics-depth comparison.

Setting CPU work flags to zero in the earlier comparison disables the selected
optimization bits; it still uses the hoisted vertex-fetch plan and compact
payload machinery. It does not validate every alternative vertex decoder.

Native screenshots also add a GPU readback/flush, and gated draw CSV logging
adds CPU work. These can alter producer/consumer timing even though normal
presentation already fences the GPU. Stable instrumented captures therefore
cannot rule out a timing-sensitive guest geometry or command-buffer problem.
The missing-model sequence should first be confirmed with logging and native
readbacks disabled, then compared with a narrowly gated trace of that failure.

## Outstanding evidence

The user has been asked which executable/launch command exhibits the issue.
This can distinguish the preserved baseline and older default build from the
main checkout's changing performance candidates. The next useful comparison
needs the affected build and scene phase, followed by a confirmed visible /
missing / visible sequence and its matching pipeline, vertex and query state.
The investigation still requires a disappearing/reappearing model
pair, matched draw/geometry state, and a causal comparison before any fix or
completion claim.
