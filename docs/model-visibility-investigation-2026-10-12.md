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

## Outstanding evidence

The user has been asked which executable/launch command exhibits the issue.
This can distinguish the preserved baseline and older default build from the
main checkout's changing performance candidates. The next useful comparison
needs the affected build and scene phase, followed by a confirmed visible /
missing / visible sequence and its matching pipeline, vertex and query state.
The investigation still requires a disappearing/reappearing model
pair, matched draw/geometry state, and a causal comparison before any fix or
completion claim.
