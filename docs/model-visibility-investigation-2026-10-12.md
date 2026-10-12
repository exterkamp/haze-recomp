# Nearby model visibility investigation

The user reports entire soldiers and airships disappearing and reappearing,
especially up close, including while neither the player nor camera moves.
The user subsequently identified the trigger as walking up to the soldiers
and starting the first conversation.
The requested investigation is on a new branch in the existing worktree:
`investigate/model-visibility`, based on `9c48009`. Its independent SDK branch
is `investigate/haze-model-visibility`, based on `9058dc4`. This preserves the
previous shadow clear correction. The investigation identifies the cause for
both reported model types; no permanent runtime fix is committed.

Both disappearance types are caused by the guest occlusion filter receiving
incorrect results from the emulated SPU arithmetic. The occlusion SPU image39
(runtime image40) rasterizes clipped model bounds using projected maximum
depth values. Ordinary host float operations overflow these to infinities
and NaNs. Real SPU single precision instead uses extended-range finite
values and saturating overflow. The resulting incorrect rejection removes
the whole model before renderer submission; this is not render distance.

Native images, matching geometry/draw captures, and model-filter records
identify speaker `0x4ACE4000` and airship `0x4ACEB6C0`. Complete offline SPU
replay reproduces all 43 accepted lists in the initial soldier recording and
all 42 in the later airship recording. Clearing the software occluder depth
does not prevent the erroneous rejection. The airship's three rejections
match three missing native frames exactly, followed by immediate returns.

An isolated extended-range arithmetic prototype restores every rejected
speaker and airship in those recorded inputs without removing any accepted
model. A live speaker comparison with normal SPU dispatch and occlusion
enabled retains the model in all 40 decisions and keeps it visible in all
40 native images through the close-up and pullback. Original-arithmetic
replay of those same live inputs reproduces nine speaker rejections.
The airship correction is validated offline, not by an equivalent live
candidate comparison. The prototype preserves ordinary host rounding and
is not a complete architectural floating-point implementation. Production
implementation and broader regression testing remain follow-up work.

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

## Mechanic absence and continuous transition check

Two additional runs use the preserved executable at 1440p with native shadows,
CPU flags 1283, depth clear 1, window position 1, shadow comparison 0 and depth
export 0. External recordings leave draw logging gated off and make no native
readback requests. Both runs exit normally with code 0.

Run `20261012-090142-558` saves 9,706 successful presents. A window screenshot
shows the mechanic crouched beside the vehicle's front wheel. The following
45-second recording starts with the mechanic absent, while the vehicle and
background align closely with the screenshot. However, the recording begins
33.02 seconds after that screenshot. There is no captured departure in this
gap, so the pair cannot establish an abrupt rendering disappearance. The
1,350-sample recording and its eight largest regional changes contain no
foreground mechanic to compare. After a further approach, a second recording
contains 601 samples over 19.99 seconds; its contact sheet and eight largest
regional changes retain the squad and airship. Native readback
`requested_009701.ppm`, taken afterward, also shows those models. The mechanic
is outside that final view.

Run `20261012-091407-463` addresses the capture gap by recording continuously
through the approach and neutral hold. It saves 7,995 successful presents.
The video contains 3,600 samples over 119.98 seconds. Movement and camera
adjustments occupy the first portion; the neutral comparison starts at sample
2268, half a second after the last neutral-input acknowledgment, and lasts
approximately 44.38 seconds. Contact sheets and the eight largest changes in
the mechanic region show him repairing the wheel, standing up, then walking
toward the player. Inspection of samples 2998 through 3006 shows his opaque
body progressively leaving through the right edge. This observed absence is
ordinary NPC movement, not a reproduced rendering disappearance. It does not
explain the user's independently reported soldier and airship pop-out, nor
prove the cause of the first run's uncaptured absence.

All selected external sample timestamps in both runs have preceding and
following successful-present records, and all present rows retain the baseline
modes above. The largest recording gaps are 0.078 seconds and 0.085 seconds
in the first run, and 0.106 seconds in the continuous recording. No decoded
sample in these three videos has all channels at or below 2. This checks for
the earlier whole-screen black state, rather than individual missing models.
The local validation is saved as `visibility-mechanic-validation.json`.

A late native capture request in the continuous run did not complete before
the scheduled shutdown. There is no native readback of that run's final state;
the screenshot helper's unchanged output still belongs to the preceding run.
The continuous transition conclusion therefore rests on the external footage.
Fixed pad-poll routes again produced different movement distances, reinforcing
the need to verify each actual view and avoid treating polling counts as a
reproducible camera trajectory. No renderer behavior was changed.

## Uncapped presentation comparison

Run `20261012-145656-069` tests the preserved executable with `frame_limit=0`.
All other graphics settings match the 1440p/native-shadow baseline, including
`vsync=auto`; startup confirms `max_fps=0` and `present_sync=1`. This isolates
removal of the configured FPS ceiling, rather than also changing VSync or the
executable. Native capture and draw logging remain unused during both external
recordings. The run exits normally with code 0. Its successful-present CSV
retains the final 16,384 records, counters 18446 through 34829; earlier menu
records have rolled out of the bounded ring. Every retained row has CPU flags
1283, depth clear 1, window position 1, shadow comparison 0 and depth export 0.

The approach recording contains 2,701 samples over 89.99 seconds, with a largest
capture gap of 0.145 seconds. It includes deliberate movement and camera turns,
and is not a stationary comparison. A following input-neutral hold near the
squad and parked airship contains 1,801 samples over 60.00 seconds, with a
largest gap of 0.090 seconds. Both contact sheets and each recording's eight
largest adjacent regional changes were inspected. The squad and parked
aircraft remain opaque in those inspected images. Native readback
`requested_034802.ppm`, taken after both recordings, also retains them.
Every external sample timestamp has preceding and following retained
successful-present records, and neither video contains a decoded sample with
all channels at or below 2. The local correlation is
`visibility-uncapped-validation.json`.

The hold spans 1,413 successful-present intervals in 60.00 seconds, despite
the removed cap. This run therefore does not test sustained gameplay above
30 FPS or rule out a higher-frame-rate failure. The external recorder also
adds overhead. Removing the cap did not produce a confirmed model visibility
failure in the inspected evidence; no causal fix follows from this comparison.
Short four-poll menu button presses were unreliable uncapped; verified menu
navigation used 40-poll presses before the gameplay recordings began.

## First-conversation sequence

Run `20261012-151150-184` targets the newly identified first-conversation
trigger using the preserved executable, automatic 1440p rendering,
follow-resolution 5120x5120 shadows, and the 30 FPS ceiling. It exits normally
with code 0 and preserves 11,357 successful presents, all with the baseline
live modes. Draw logging remains gated off throughout. Native capture is
unused during the two external recordings. A separate main-checkout game
runs concurrently, so these recordings are not performance comparisons.

The first recording contains 5,401 samples over 179.99 seconds and follows
the approach, pauses near the squad, the actual first conversation, automatic
camera turns and aircraft flyover, then the "Board your squad's dropship"
objective. Every external timestamp has preceding and following successful
present records; the largest sample gap is 0.112 seconds. A 60-sample overview
of the whole recording was inspected, together with the eight largest
regional changes in the closest pre-conversation hold and the eight largest
changes in the final post-conversation segment. Those inspected comparisons
do not confirm an individual model disappearing and reappearing.

Reviewing the conversation segment itself does reproduce the reported
appearance in the external recording. Full-size context around samples
3240 through 3243 shows the close helmeted soldier present at sample 3241
and entirely absent at 3242, while the deck, foliage, sky and parked aircraft
remain visible. Context around 3260 through 3263 shows him absent at 3261
and present again at 3262. The absent samples span approximately 0.65 seconds;
the two observed transition samples are approximately 0.70 seconds apart.
This occurs during automatic conversation camera movement, before the later
short player-input command. It is not explained by the NPC walking out of
the viewport. The parked aircraft is also absent at samples 3769 and 3770
and present again at 3771 and 3772; the camera changes angle slightly across
that pair, so its exact view is not fixed.

| Model transition | External samples | Recording offsets | Presentation brackets |
| --- | --- | --- | --- |
| Close soldier disappears | 3241 to 3242 | 108.017 to 108.052 seconds | 5766/5767 to 5767/5768 |
| Close soldier returns | 3261 to 3262 | 108.704 to 108.747 seconds | 5781/5782 to 5782/5783 |
| Parked aircraft returns | 3770 to 3771 | 125.671 to 125.693 seconds | 6170/6171 to 6171/6172 |

These brackets locate the external samples among successful presents;
they do not prove which individual present supplied the sampled pixels.
Contact and adjacent-pair review of the conversation identifies the target,
but there is no native readback or per-draw trace of these specific missing
states yet. Local full-size paired contexts are
`visibility-conversation-soldier-out-context.png`,
`visibility-conversation-soldier-return-context.png` and
`visibility-conversation-aircraft-return-context.png`. Timestamp correlation
is saved as `visibility-conversation-model-correlation.json`.

The script begins shortly after the fourth approach step's neutral-input
acknowledgment, around recording offset 95.97 seconds. A subsequent short
forward command occurs during the script around offset 128.58 seconds.
Thus neither the entire recording nor the entire conversation is an
input-neutral comparison. Automatic script camera movement also prevents
treating neutral player input as a stationary viewpoint.

Two whole-scene black intervals are observable during the conversation:

| External samples | Recording offsets | Successful-present brackets |
| --- | --- | --- |
| 2879 through 2898 | 95.970 through 96.587 seconds | 5529 through 5540 |
| 4167 through 4238 | 138.884 through 141.273 seconds | 6436 through 6481 |

For these samples, every decoded channel in the top 540 of 720 rows is at
most 2; the lower HUD is excluded. The second interval leaves the squad-join
messages visible before the 3D scene returns. These are external observations,
not native readbacks of the black state. The listed timestamps are the first
and last qualifying samples, not exact transition boundaries.

Unlike the earlier black run `20261012-083842-069`, the conversation's black
intervals do not coincide with a collapse in submitted scene work. Around
the second interval, the cumulative renderer summaries imply approximately
1,317 executed groups per frame between summaries 6432 and 6464, and 1,341
between 6464 and 6496. Seen and executed group totals match, with zero movie
and queue-full counts in the inspected summaries. These aggregate counters
do not identify visible models or establish why the image is black. A scripted
fade and a rendering defect have not been distinguished, and neither interval
is yet linked to the user's individual-model report. Local correlation is
saved as `visibility-conversation-black-correlation.json`.

Native readback `requested_008224.ppm`, taken after the first recording,
shows three opaque nearby squad members, the vehicle and parked aircraft.
The subsequent input-neutral recording contains 1,350 samples over 44.96
seconds, with a largest gap of 0.088 seconds. Its contact sheet and eight
largest regional changes retain the nearby soldiers. The aircraft takes off
and leaves through the upper edge during the opening portion; its later
absence is not evidence of a rendering disappearance. All sample timestamps
have presentation brackets. The native frame's two adjacent presentation
records retain the baseline modes. Validation is saved as
`visibility-conversation-validation.json`. No renderer behavior was changed.

A follow-up attempt, `20261012-153154-964`, repeats the saved pad-poll route
but overshoots the target and does not reach the conversation in either
recording. Its black-triggered native/trace requests never activate. It exits
normally with code 0. This is an unsuccessful navigation attempt, not evidence
that instrumenting the reproduced scene fixes or preserves its model visibility.

## Native first-conversation reproduction

Run `20261012-154146-932` reaches the conversation using smaller approach
steps verified with actual screenshots. It retains the same preserved
executable, 1440p/follow-shadow graphics settings and baseline modes, exits
normally with code 0 and saves 11,841 successful presents. Menu navigation
sent before the title was ready had to be repeated; the actual gameplay view
was verified before the approach.

There are 189 consecutive native readbacks, `requested_008767.ppm` through
`requested_008955.ppm`. The initial black-triggered requests account for nine
frames; a subsequent requested burst accounts for 180. Every captured counter
has both its same-number and next-number successful-present record, with no
live mode switch. The normal native readback precedes presentation, so native
frame N contains draw-frame N and is associated with successful present N+1.

Inspection of the native contact sheet and full consecutive context around
the close soldier confirms the disappearance:

| Native frame(s) | Observed close soldier |
| --- | --- |
| 8837 | Visible |
| 8838 through 8840 | Missing; background remains visible |
| 8841 | Visible again |
| 8842 through 8847 | Missing; background remains visible |
| 8848 | Visible again |

This reproduces the model-specific appearance in the renderer's native image,
rather than only an external window capture. It does not identify whether the
model was omitted, transformed incorrectly, clipped or rejected by pixel/depth
state. The native images also show the initial whole-scene black interval,
with black top-three-quarter regions in frames 8767 through 8770; this remains
a separate observation from the missing soldier.

The draw gate covers only frames 8767 through 8790, with 28,160 draw rows
over 24 counters, and ends before the soldier's missing frames. A shared gate
was closed by the black-capture helper before the later native burst ended.
There is therefore **no matching per-draw trace of the missing soldier in this
run**. Readback and logging substantially perturb timing; the recorded native
burst takes approximately 40.66 seconds, so its counter offsets cannot be
converted to the original recording's elapsed time. Local artifacts are
`visibility-conversation-target-native-close-context.png`,
`visibility-conversation-target-native.review.json`,
`visibility-conversation-target-trace-coverage.json` and
`visibility-conversation-target-validation.json`.

## Matched native frames and draw trace

Run `20261012-163003-500` captures the close conversation with a single
helper owning both the native requests and draw gate. Its color threshold
only locates the large yellow helmet in the known conversation sequence;
the disappearance is then verified visually, rather than inferred from that
threshold. The preserved executable and baseline graphics/modes are retained.
It exits normally with code 0, saving 11,303 successful presents.

The 48 consecutive native frames cover 7596 through 7643. The soldier is
visible in 7599, absent in 7600 through 7603, visible in 7604, absent in 7605
through 7608 and visible again in 7609. The deck, foliage and parked aircraft
remain visible during the missing states. The draw CSV covers counters 7596
through 7644 with 54,784 rows, all `execute`; gate boundary counters may be
partial. The three adjacent comparisons below are entirely inside that gate.

| Native / draw frames | Close soldier | Executed draw rows |
| --- | --- | --- |
| 7599 to 7600 | Visible to missing | 1,052 to 1,044 |
| 7603 to 7604 | Missing to visible | 1,025 to 1,072 |
| 7604 to 7605 | Visible to missing | 1,072 to 1,060 |

Grouping by surface, topology, source vertex count and fragment-program
identity, while excluding changing PSO hashes, identifies two candidate
batches. Each occurs once in the inspected visible frames and zero times in
all eight missing frames listed above:

- Target `0x014D0000`, triangle topology 5, 4,596 source vertices.
- Same target and topology, 336 source vertices.

Both use fragment program `0:0x02C4ADC0`, size 1,152 bytes, and live pipeline
key `25ae28ab4252aa73` when present. Their textures include unit 0 at
`0x05CEF980` (512x512), unit 1 at `0x09E81F80` (512x512), and unit 2 at
`0x04E61980` (256x256). Their stable non-position attribute fingerprints and
repeated material bindings give a more specific candidate than aggregate
scene counts. These are still mesh candidates, not named model IDs; a scoped
suppression or geometry identification is needed to prove they are the close
soldier rather than another object changing in sync.

The renderer's surrounding summaries at 7584 and 7616 show seen groups equal
executed groups, zero empty groups and zero fetch, degenerate, primitive,
allocation, pipeline, ring or surface drops. Packet counts also balance, with
zero movie and queue-full counts. Source inspection confirms that fetch and
topology failures can return before a CSV row, but increment these drop
counters. No such failure is recorded around this sequence. Consequently,
the candidate batches' absence is not explained by a recorded backend draw
rejection. If their model identity is confirmed, the next boundary to inspect
is command production/culling before these batches reach the live renderer,
rather than changing depth or fragment shading on their absent draws.

Every native capture has same-number and next-number successful-present
records, and all present rows retain the baseline live modes. The external
recording contains 5,259 samples over 179.84 seconds, all bracketed by
successful presents. Its largest sample gap is 0.833 seconds during the
instrumented sequence, so it cannot establish every intermediate state.
The 48 requests span approximately 23.24 seconds while tracing is active;
this remains a timing-perturbed diagnostic. Local evidence is
`visibility-conversation-model-trace-native.contact0.png`, the three
`visibility-conversation-model-trace-{out,return,out2}.json` comparisons,
`visibility-conversation-model-trace-normalized.json`,
`visibility-conversation-model-trace-target-texture.json` and
`visibility-conversation-model-trace-validation.json`.

## Material-identification probe

Run `20261012-164245-248` tries the existing debug setting
`LD_SKIP_SAMPLING=5CEF980` to identify the candidate material visually. It
exits normally with code 0. The probe does **not** select the candidate:
a short draw trace in this run locates the same 4,596- and 336-vertex
batches at unit-0 texture address `0x05DC8F00`, rather than `0x05CEF980`.
Their attribute-8 fingerprints remain `ab7c74af214e9569` and
`bf3cd8c23d609017`, respectively. Consequently, the still-visible soldier in
native frame 4628 does not refute the candidate identification: the fixed
offset was stale. Frame 4793 supplies the relocated bindings in a trace
covering partial counters 4793 and 4794. Local correlation is
`visibility-conversation-identify-validation.json`.

An isolated diagnostic build adds `LD_SKIP_SAMPLING_FILE` to select a measured
texture address during the same run. It also records deliberate suppression
as `probe_skip_sampling` CSV outcomes and a separate counter included in group
accounting. Missing/invalid control files and the text `off` disable the live
filter. The preserved executable retains SHA256 `EF9CF6AB...`; the diagnostic
candidate is `local/candidates/visibility-live-filter/HazeRecomp.exe`, SHA256
`541137D645B6265B78F5CBC3781F83619C906EB41EB5D75200CE86CB326A2067`.
Only the SDK renderer object and runtime archive were rebuilt, then linked
against the existing game objects into that separate candidate. The SDK
diagnostic change is not a model-visibility fix.

The first diagnostic attempt, `20261012-165408-595`, closes normally during
loading after menu navigation had to be repeated. It preserves 13,556
successful presents but never reaches the model/filter comparison. Its
observed UI summaries have zero probe skips and balanced group accounting
with the control set to `off`. The active-filter behavior and causal model
identification remain unvalidated by that attempt.

## Scoped source findings

The SDK source below is from the investigation's preserved `9058dc4` base.
These checks narrow hypotheses; none establishes the reported failure.

- **Black-screen guest correlation:** In external run
  `20261012-083842-069`, fifteen low-address reads attributed to guest function
  `00722E98` first appear between renderer summaries 4928 and 4960, shortly
  after the externally observed black transition around presents 4926/4927.
  The reads alternate between `0x33B` and `0x32B`. Generated code confirms that
  this function follows the stack backchain and copies saved return addresses;
  these are invalid stack-walk reads, not direct model or occlusion reads.
  Its two direct callers request six and 32 entries respectively. The captured
  executable lacks the later optional null-read stack-context diagnostic, so
  this log cannot identify which caller or invocation produced the reads.
  The generic reporter reaches its 16-message cap (including one boot read),
  so later silence is not evidence of recovery. The next three external runs
  each report only the known boot read and do not reach that cap. All recorded
  group-drop counters, movie packets and queue-full counts remain zero in the
  black run through its final renderer summary at frame 5568; incoming packets
  and executed groups both fall. This narrows the whole-screen observation
  beyond a dropped-draw explanation, but does not establish causality or link
  it to individual model pop-out. The local correlation is
  `visibility-black-guest-correlation.json`. If this transition recurs, capture
  a native image and bounded caller context before changing guest behavior.
- **Texture-cache capacity:** `libs/video/rsx_live_draw.c:2916` handles the
  4,096-entry limit by replacing the least recently used descriptor and
  retiring its old resource after the open command list reaches a fence.
  The logged `full` counter counts encounters that select this eviction path;
  it does not mean new textures were rejected. The black run's final summary
  has zero texture decode failures and zero white-texture bindings. Cache
  occupancy alone therefore supplies no demonstrated disappearance cause.
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

## Live material suppression comparison

The SDK now extends its existing debug-only `LD_SKIP_SAMPLING` filter with
`LD_SKIP_SAMPLING_FILE`. A file containing a hexadecimal texture offset selects
draws sampling that offset in any enabled texture unit. `off`, an invalid
value, or a missing file disables the live filter. It polls every 512 draws,
so a file change can take effect partway through a frame. CSV outcomes record
`probe_skip_sampling`; the cumulative heartbeat includes its separate count
in group accounting. The existing fixed-address filter also gains this
accounting. Neither environment variable is set by normal launch scripts.
This is an identification tool, not a model-visibility fix.

The diagnostic executable is isolated at
`local/candidates/visibility-live-filter/HazeRecomp.exe`, SHA256
`541137D645B6265B78F5CBC3781F83619C906EB41EB5D75200CE86CB326A2067`.
It rebuilds the SDK renderer and links the existing game objects. It does not
recompile the shared, concurrently changing generated game code. The preserved
baseline executable still has SHA256 `EF9CF6AB...B06B7B`.

Run `20261012-170639-237` reached gameplay but did not reach the conversation
before its scheduled close. The live control remained `off`, its candidate
observer timed out without a match, and it exited normally with 14,296
successful presents. This run supplies no on-mode model-identification result.

Run `20261012-171656-287` initially reached a whole-scene black state during
navigation. Native frame 5269 confirms the black output. This is separate
from the individual soldier disappearance. Restarting the checkpoint restored
the scene. After a shorter approach, the first conversation triggered and
the observer found the matching 4,596-vertex / `ab7c74af214e9569` batch at frame
9458. Its unit-0 texture was now `0x04894300`, 128x128, rather than the earlier
run's 512x512 material address.

Five off/on/off/on/off phases captured 20 native frames, 9458 through 9477.
The CSV records exactly 18 suppressed groups: nine of the 4,596-vertex batch
and nine of the 336-vertex batch with UV hash `bf3cd8c23d609017`. The control
changes are logged at frames 9462, 9466, 9470 and 9474. The first phases overlap
the conversation's black transition; the later phases overlap its moving
camera and show two close soldiers. These images do not identify the batches
as the disappearing soldier. An aircraft also briefly disappears and returns
within the second on phase, so that transition alone cannot be assigned to
turning the filter on or off. Group accounting remains balanced; the final
skip count is 18. The process was closed normally. Evidence is preserved in
`visibility-conversation-live-identify3-probe.json`, its draw CSV, and
`visibility-conversation-live-identify3-repeat-context.png`.

Run `20261012-172912-985` repeats the diagnostic at 1440p scene / follow shadows,
30 FPS cap, baseline depth/shadow/CPU controls. Its first five-phase comparison
captures 40 native images, 7091 through 7131 with one gap. It again overlaps
the scripted camera sweep and fade, making visual identification ambiguous.
The chosen offset `0x0819A600` suppresses 32 groups across the two on phases.

A second comparison in the same run occurs after the conversation. The player
turns toward three helmeted soldiers and approaches them, then retains neutral
input throughout the five phases. It captures another 40 native images across
11355 through 11401. The helper now requires a candidate in the most recent
two trace counters and refreshes its material address before each on phase.
This matters even within one run: the first offset, `0x04D29280`, ceases to
match after two frames; a later on phase selects `0x04CCC300` instead.

The second on phase demonstrably suppresses both matching batches throughout
frames 11386 through 11393. Native frame 11390 shows all three nearby helmeted
soldiers still opaque. Frame 11398, after both batches execute again, also
shows all three. Thus this comparison does not causally identify these two
batches as any of the three visible bodies. It weakens the inference from
the earlier temporal correlation; a mesh fingerprint is not an individual
model-instance identifier. The first on phase's representative image 11370
is not a valid suppressed-frame comparison because streaming has already
changed its selected address. There are 22 suppressed groups in this second
comparison and 54 cumulatively, with balanced renderer ledgers and zero
recorded rejection counters.

The run reaches its 600-second runner limit and is terminated before the
attempted manual close. Its present CSV therefore is unavailable; do not claim
successful-present timing validation for these 80 images. The native frames,
draw counters, live-filter transitions and effective graphics settings remain
available. Local evidence includes `visibility-conversation-live-identify4-`
`post-probe.json`, `post-contact.png`, `validation.json` and the draw CSV.
Both helpers finished and reset the filter to `off` before the runner ended.

The next useful identification step is a narrowly gated vertex/transform
capture for the candidate and the other character draws, so their projected
positions can be compared with the missing body's screen region. Suppressing
a material without locating its instance is insufficient to diagnose culling.

## Projected geometry and body submission evidence

Run `20261012-174837-480` uses an isolated diagnostic executable, SHA256
`BB43C23CAA5717D26B344BFC3BF774147B691A517DE95BB25E7D8B556A0DD4C3`,
with 1440p rendering, follow shadows and the baseline 30 FPS controls. It exits
normally with 13,083 successful presents. All 40 native captures and their
following present counters retain CPU-work mask 1283, shadow compare off,
depth export off, window-position correction on and depth-clear correction on.

The new opt-in `LD_TRACE_SOURCE_VERTS=4596` selector uses the existing geometry
dump and gate, capturing three matching draws at counters 8489, 8490 and 8491.
No matching dump occurs before the gate opens. Each dump contains 937 compact
vertices, the vertex program and its constants. Source occurrence count and
compact unique-vertex count differ because repeated indices are compacted.
The candidate's position path divides attribute 0's xyz by w and applies
constants 0 through 3, followed by the dumped viewport adjustment. Projecting
trace 2 onto native image 8490 produces a facial point cloud inside the opaque
yellow helmet. Its screen bounds are approximately (592,69) to (843,458) in
the 1280x720 image; all 937 points lie inside the clip volume. This identifies
a small concealed head/face part, not the complete body. The 336-vertex partner
has not independently been spatially identified.

Five off/on/off/on/off phases capture native frames 8490 through 8530 with one
gap at 8514. The filter suppresses exactly 22 groups across two on phases;
its selected unit-0 texture remains `0x05421900`, 512x512, throughout this
window. The renderer ledger balances with zero recorded rejection counters.
The second on phase, 8515 through 8522, skips both candidate parts while three
helmeted bodies remain opaque. This agrees with the projected head geometry.
Early on-phase frames 8498 through 8502 have no candidate submission, so their
missing soldier cannot be attributed to the filter.

The initial off phase provides another natural disappearance: native frames
8490 through 8492 show the close speaker, 8493 through 8496 show the background
in his place, and 8497 shows him again. All eight images have been inspected
together. In the missing interval the following four mesh fingerprints have
no submissions; before and after it they execute:

| Source occurrences | Attribute-8 fingerprint | Role supported by evidence |
| ---: | --- | --- |
| 11,691 | `de6c73babf52be53` | Body-material candidate |
| 2,628 | `aadd5d83165013c6` | Body-material candidate |
| 4,596 | `ab7c74af214e9569` | Projected concealed head/face part |
| 336 | `bf3cd8c23d609017` | Partner sharing head material |

At frame 8490 all four share constant fingerprint `52161b78a546eca4`.
The body pair uses live PSO `4be66c83713805c5`; the head pair uses
`25ae28ab4252aa73`. Another instance of the body mesh has different constants.
Revisiting baseline run `20261012-163003-500` finds the same four fingerprints
sharing constants `1c3a9d6bb0cb2876` at visible frame 7599, with all four absent
at missing frames 7600 through 7603. Other soldiers reuse the body meshes,
which explains why asset-wide counts alone previously missed this lead.
Constant equality is a within-frame association, not a stable actor ID across
frames. Causal suppression or spatial capture of the body remains useful.

The player input remains neutral through this run's initial off phase, but
the conversation controls the camera. A later movement route overlaps the
second on and final off phases; do not describe the full comparison as having
a stationary camera or neutral input. Native readback and tracing also perturb
timing. Local evidence is under prefix `visibility-conversation-close5`,
including `natural-context.png`, `geometry-overlay.png`, `validation.json`,
`close-probe.json`, the geometry folder and draw/present CSVs.

Static analysis supplies a more specific upstream lead. The string
`noocclude` at guest VA `0x00C6F7D8` is referenced through table word
`0x00E1E900`. Resolving the small TOC correctly identifies property parser
`func_0052A4E0`: its call at return address `0x0052A720` parses the boolean
into stack byte +0x73 and packs it into bit `0x400` of the model's +0x4C flags.
`func_00526B48` transfers that bit to runtime flags +0x5C as `0x00800000`
when its object-creation condition holds. `func_006ABD38` tests that runtime
bit while building a bounds-related list, and has an initial enable-byte test
at guest `0x0169F608`. These are static leads, not proof that the missing
speaker traverses that path or that this byte controls all visibility culling.
The earlier guessed functions `0058AB70` and `005FCA28` do not resolve to this
property table and must not be used as noocclude references.

Run `20261012-181417-235` attempts a body-material comparison using the same
diagnostic executable. Navigation does not produce a comparable close-up; the
240-second external observer records 7,201 samples and never reaches its
close-helmet trigger. The filter stays off, and this run supplies no body
suppression or geometry-identification result. It exits normally.

A read-only process-memory check, restricted to this run's exact PID and
executable path, independently confirms small TOC `0x00E368E0`, its pointer at
`0x00E2E948` to global block `0x0169F484`, and enable byte `0x0169F608 = 1`.
At the end, a local helper writes this byte to 0 and immediately restores it
to 1, verifying both writes by reading back the byte. This only validates
the diagnostic control mechanism; there is no visual comparison during that
brief toggle and no claim that it changes model visibility. The process is
then closed. The local transition record is
`visibility-conversation-body6-byte-transitions.jsonl`. The VM allocation in
this executable is not 4 GB aligned, so the helper identifies it using the
known entry descriptor and validates the pointer chain rather than assuming
the current SDK header's allocation alignment applies to older game objects.

## Native airship disappearance and guest enable-byte comparison

Run `20261012-182638-760` reproduces both kinds of disappearance in native
captures during the first conversation. Frames 7974 and 7975 show the close
speaker and the parked airship. In 7976 the airship is absent while the speaker
remains visible; 7977 shows the airship again. In 7978 through 7981 the speaker
is absent while the airship remains visible. All eight images were inspected
together. Matching draw CSVs cover this interval, with the material filter off.
The run exits normally after 10,627 successful presents. Comparing source-count
and attribute-8 fingerprints across 7975/7976/7977 produces multiple candidate
airship parts, but none has yet been spatially identified. Do not treat changing
vertex-program or PSO fingerprints alone as evidence of a missing mesh.

The initial enable-byte probe in that run fails after its first eight captures:
the VM discovery helper overwrites a caller's `base` path variable with an
allocation address. The first phase never writes byte 0; cleanup restores 1.
After correcting the helper, a second probe successfully records five phases
of eight native images each and verifies the 1/0/1/0/1 byte writes. This occurs
after the close conversation camera phase and shows visible soldiers in all
conditions. Its trace gate also differs from the run's configured gate, so the
retry supplies no matched draw trace. These limitations prevent a causal claim.
The local evidence uses prefix `visibility-conversation-byte7`.

Run `20261012-183512-733` repeats the corrected probe at the first close helmet
peak, with the proper trace gate. All 40 native frames have matched draw CSVs;
their present counters N and N+1 retain the baseline modes. The writes to guest
byte `0x0169F608` and phase-end readbacks are verified, and cleanup restores 1.
Frames 7197 through 7200 show the close speaker with the byte enabled. In 7201
through 7204 he is absent, with no submissions for the associated body/head
mesh candidates. The first disabled phase, 7205 through 7212, shows him again.
Later enabled and disabled phases, through 7236, show visible bodies as the
scripted camera pulls away and turns. Thus the initial return coincides with
disabling the byte, but the later phases do not repeat the disappearance. This
is timing-confounded evidence, not proof that this control fixes the defect.
Player input is neutral during the comparison; the conversation camera moves.
The run exits normally after 9,521 successful presents. Local records use
prefix `visibility-conversation-byte8`, including `byte-probe.json`,
`contact.png`, and `validation.json`.

Both runs emit three body-source geometry trace messages, but their output
directories were created too late or not created at all. The trace writer does
not create its directory, and the three-draw limit is consumed even when the
input-file open fails. Consequently neither run supplies valid body input
geometry for projection. The next capture must create its directory before
opening the trace gate. No body geometry identification is claimed here.

Run `20261012-184225-880` reverses the phase order to 0/1/0/1/0, setting the
byte to 0 after loading and before the approach. Native frames 15459 through
15466 show the close speaker with the byte disabled. In the first enabled
phase, 15467 and 15468 show him, 15469 through 15472 show the background in his
place, and 15473 and 15474 show him again. Frames 15475 through 15498 show
visible bodies in both conditions as the camera pulls away. All 40 images were
inspected together; their matched draw trace contains only `execute` outcomes,
and present modes at N and N+1 retain the baseline values. The head pair has no
submissions in 15469 through 15472. Other body instances are still submitted
in 15469 and 15472, and none of the four associated mesh candidates is submitted
in 15470 or 15471. The renderer ledger balances with zero rejection counters
and zero diagnostic sampling skips. The probe verifies all byte writes and
readbacks, restores its original 0, and a separate verified write restores 1
before normal process closure. The run exits normally after 15,956 presents.

This reversed order makes the bounds-list control a more promising lead, but
the same short close-camera interval is still a potential confound. Holding
the byte disabled through that entire interval is the next comparison; the
two phase-order runs alone do not establish causation.

Creating the geometry directory before this run preserves all three valid
11,691-source traces. Each has 2,846 compact vertices. Traces 1 and 2 correspond
to draw frame 15458; trace 3 corresponds to native frame 15459. The emitted
vertex shader's position path divides attribute-0 xyz by its w, transforms
with constant vectors 0 through 3, then applies viewport scale/offset. Projecting
trace 3 through that path onto native 15459 places points on the speaker's
helmet, shoulders and torso. It confirms that this mesh is part of the visible
armored soldier, rather than only the concealed face identified previously.
Of its vertices, 961 project inside the clip volume; the full mesh extends
below the close-up image. Trace 2 is an off-screen instance. Local evidence is
under prefix `visibility-conversation-byte9`, including `body-overlay.png`,
`byte-probe.json`, `validation.json`, and the geometry directory.

Run `20261012-185439-387` holds the byte disabled through the first three
eight-image phases. Native frames 5008 through 5031 show the speaker throughout
the close-up and subsequent pullback, including the interval where the phase
order comparisons lose him. The associated body and head batches remain
submitted throughout. The later enabled phase, 5032 through 5039, and final
disabled phase, 5040 through 5047, also show visible soldiers after the close-up.
The byte writes and phase-end readbacks are verified; cleanup restores 0 and
a separate verified write restores 1 before closing the game. All 40 images
were inspected together. Matching CSVs contain only `execute` outcomes, and
all native counters N and N+1 preserve the baseline modes. The process exits
normally after 7,122 successful presents. Its external observer completes
180 seconds and preserves 4,120 samples; instrumentation reduces the achieved
sample rate. Local evidence uses prefix `visibility-conversation-byte10`.

This comparison strengthens the bounds/occlusion-path lead but requires a
clean repeat: an additional 150-poll forward command was sent after the scene
had already reached its black conversation transition, and overlaps the first
three capture phases. Its following neutral command starts at QPC
1343531.8441235. The scripted view still follows the recognizable close-up and
pullback, but this run must not be described as neutral-input throughout.
The preceding reversed comparison did keep input neutral through its captures.

Static follow-up connects this bounds list to guest worker job type 11 through
`007ED838` -> `00682648`. The job table at `0x00DFBDDC` resolves entry 11 to
OPD `0x00E6DE30`, code `00682BC8`; the separate mode byte is 2. This function is
the PPU fallback, not proof that the default job executes it. The fallback clears
a 0x40000-byte buffer through `007ED660`, processes geometry with `007ECDD0`,
prepares data with `007ED590`, then tests each bounds pair with `007ED6D8` and
compacts accepted identifiers. This supplies concrete producer/consumer
functions for further tracing. It does not establish that a particular
worker result caused a captured disappearance, and the guest job number must
not be confused with an SPU image number.

## Neutral-input repeat and CPU occlusion buffers

Run `20261012-190500-222` repeats the held-off comparison with the extra
movement removed. Guest byte `0x0169F608` is set to zero before approaching
the squad. The final forward command is followed by neutral input starting
at QPC 1344189.6552457, before the observer's close-helmet trigger at
1344201.2189819. No further input is sent through the comparison.

The five phases are again 0 / 0 / 0 / 1 / 0, with eight native images per
phase. Disabled phases cover counters 6993..7000, 7001..7008 and 7010..7017;
the enabled phase covers 7019..7026, and the final disabled phase covers
7027..7034. The gaps are not captured frames. The speaker remains visible
through all 24 images in the initial disabled interval, including the full
close-up and pullback. All 40 images have matching draw/present records;
their N and N+1 renderer modes retain the baseline settings. The four
identified body/head mesh fingerprints submit in every captured frame.
The draw trace contains 48,640 `execute` outcomes and no other outcomes.
The process exits normally after 12,792 successful presents. All byte writes
are read back, and the original enabled state is restored before closing.
Local evidence uses prefix `visibility-conversation-byte11`. This removes
the preceding run's input confound, but does not itself identify the exact
worker decision responsible for a missing speaker.

Read-only memory inspection also locates the live CPU occlusion data.
`vm_read32(0x00E94EE0 - 0x7D5C)` points to a small TOC whose -0x8000 entry
points to the 16-entry frame-context pool; context stride is 0x4F0. Context
+0x4C0 points to the 0x89B00-byte worker buffer. Relevant buffer offsets are:

| Offset | Data |
| --- | --- |
| 0x00000 | Four-row world view-projection matrix |
| 0x00040 | Software raster depth buffer, size 0x40000 |
| 0x7AAD0 | Input min/max bounds, stride 32 bytes |
| 0x82AD0 | Input identifiers |
| 0x83AD0 | Accepted identifiers |
| 0x84AD0 | Input count |
| 0x84AD4 | Accepted count |
| 0x84AF8 | Occluder geometry count |

Identifiers include both raw model pointers and encoded submodel IDs; they
must not all be dereferenced as model pointers. `006ABA30` consumes these
results after `006ABD38` waits for the worker, then compacts the model array.

Two post-conversation snapshots preserve these buffers under prefixes
`visibility-byte11-post` and `visibility-byte11-current`. The latter contains
74 bounds, 42 accepted identifiers and 19 occluder entries. Projecting its
world bounds using the buffer's matrix produces boxes over the visible
soldiers. For example, raw model `0x4ACE3F60` projects to approximately
(365,342)..(478,518) in the 1280x720 external image, surrounding the left
foreground soldier, and is in the accepted list. This associates the live
data spatially with the scene. The snapshot and external image are taken
sequentially and are not synchronized with a native draw counter. Neither
snapshot captures a known disappearance; neither proves a rejection bug.

## Exact speaker-model rejection and PPU fallback replay

Run `20261012-192914-365` captures 40 consecutive native images, counters
4270..4309, during the first conversation with neutral player input. The
final movement ends before the close-up. The speaker is visible in 4270..4273,
missing in 4274..4276, visible in 4277, missing in 4278..4282, then visible
from 4283 through the pullback. The nearby airship remains visible in this
particular sequence. This run does not trace an airship rejection.

A separate diagnostic executable wraps `006ABA30` with a read-only snapshot
before model-array compaction and another afterward. It captures the frame
context, software depth buffer, bounds and identifier lists after the caller
has waited for the worker, plus the actual pre/post model arrays. This
instrumentation writes local files and changes timing. It does not write
guest memory or registers; the original function and trampoline are preserved.
The preserved executable and generated PPU source are unchanged. Diagnostic
executable SHA256 is
`8928DDCC30061C4B6BCAF8DC2CB7D939F4C8F368235C6122E6703261B97BB268`.

There are 43 saved filter decisions. Raw model `0x4ACE4000` occurs in every
input list. It is absent from the accepted list and removed from the actual
model array at consumer counters 4273..4275 and 4277..4281. It remains in the
array in the other 35 records, including 4276 and 4282. The one-native-frame
delay maps these eight removals exactly onto the eight missing native images.
This establishes the guest filter as the immediate cause of this soldier's
missing submissions, beyond the earlier mesh correlation.

The model association is spatially verified. Geometry trace 2, from draw
counter 4269, projects onto the helmet, shoulders and torso in native 4270;
973 vertices are inside the clip volume. Transforming its original guest MVP
positions back through the saved world view-projection matrix puts the body
vertices inside the recorded `0x4ACE4000` bounds. The recovered world range is
approximately (-55.947,0.991,-15.067)..(-55.122,2.864,-14.510), inside the
recorded bounds (-56.074,0.983,-15.145)..(-55.097,2.922,-14.421).
This run's trace 3 is an off-screen instance, unlike the earlier byte9 run.
Viewport scale/offset is used for the native overlay, but must not be applied
before inversion of the guest world matrix.

Matching draw CSVs contain 45,568 `execute` outcomes and no other outcomes.
The native counters and their N+1 present counters retain baseline renderer
modes. Draw ledgers balance, rejection counters are zero, and texture-sampling
suppression is off. The process exits normally after 8,226 successful presents.
Local evidence uses prefix `visibility-conversation-decision13`, including
the native images, `validation.json`, body overlay, and 43 binary decisions.

An offline executable replays the saved bounds through the original lifted
PPU functions: face culling, transformation, polygon clipping, projection,
triangle rasterization and the software-depth comparison callback. It loads
the original guest constants and captured context/depth buffer. The completed
replay accepts `0x4ACE4000` in all 43 records, including every recorded
rejection. It does not substitute an always-visible raster callback. The
replay comparison is saved as `replay-comparison.json` alongside the records.
Using the saved older view also accepts the rejected model. The camera is
outside the bounds in the rejected close-up; a camera-inside explanation is
not established. Neither a near-plane clipping defect nor an old/new-view
mismatch has been demonstrated.

Dispatch inspection explains why PPU replay alone cannot identify the faulty
instruction. `00682648` resolves mode 0 to an inline PPU call through
`006822D8`, mode 1 to the PPU worker queue, and mode 2 through `0071F788` to
`cellSpursQueuePushBody` (import NID `92CFF6ED`). The live job-11 mode byte
`0x00CA23BB` is verified as 2. Its SPU module-table entry points to an SPU ELF
at guest address `0x00F2C100`, entry 0x3080. The guest job number is not an
SPU image number. The exact active SPU implementation and its input state
must be identified before attributing this disagreement to a lifted opcode.

An initial live mode-0 attempt, run `20261012-195614-390`, diverged from the
intended approach route and did not capture the target conversation. It is
not evidence that the fallback fixes visibility. Only job 11's mode byte was
changed, with global occlusion enable verified as 1; mode 2 was restored
before closing. The next comparison will retain normal dispatch during the
approach and switch immediately before triggering the conversation.

## Live PPU comparison and deterministic SPU replay

Run `20261012-200320-879` retains default dispatch during navigation, then
switches only job 11's mode byte from 2 to 0 immediately before walking into
the conversation trigger. The write is read back at QPC 1347663.6246052;
the final neutral command starts at 1347665.2588132. Global occlusion enable
`0x0169F608` remains 1, verified both at the mode write and during capture.
There is no input after the black transition. The external helmet threshold
starts the capture at 1347688.0475473, and the gate closes at
1347709.3422569. Timing differs from the default run, so these captures are
aligned by the scripted scene and native counters, not elapsed seconds.

All 40 consecutive native images, 6182..6221, show the speaker through the
close-up and pullback. All 43 filter records retain `0x4ACE4000`; none removes
it. The same four body/head mesh fingerprints submit in every captured frame.
Matching draw traces contain 46,080 `execute` outcomes and no other outcomes;
N and N+1 present modes preserve the baseline settings. Ledgers balance and
all recorded renderer rejection counters remain zero. Mode 2 is restored and
read back after capture, before normal exit with 6,766 successful presents.
The external observer completes 180 seconds and 5,401 samples. Local evidence
uses prefix `visibility-conversation-decision15`. This is a live fallback
comparison with occlusion enabled, rather than another global filter bypass.

The live SPU ELF at guest address `0x00F2C100` matches the complete extracted
`spu_0039_at_00F1C100.elf` byte for byte. The filename records its offset in
the original PPU ELF file, not its guest virtual address. Its runtime image
ID is 40, entry 0x3080, and the 22,224-byte ELF has SHA256
`9E2CFD00BE69653E7EDB3CF45095D0FBC0F4839D9100F80CBC21BDDA2715E9D0`.
This identifies the implementation without confusing guest job 11 with an
SPU image number.

A second offline harness loads that ELF's original constants into an SPU
local store, copies the captured worker block at buffer +0x6AA50 into LS
0x18000, and runs the original lifted bounds/filter function at LS 0x4DE0
with its 96-function dependency closure. DMA reads use the captured guest
buffer, with checked extents; the job's 256-wide floating-point depth data
is read in tiles by the original code. The accepted identifiers are compacted
by the original SPU function. No always-visible callback is substituted.

For all 43 default-path records, the replay's entire accepted identifier list
matches the recorded list exactly, including the eight missing-speaker cases.
This reproduces the decisions without live job scheduling. Clearing only the
captured depth buffer to zero still rejects the speaker in precisely the same
eight records. The local comparison is `spu-replay-comparison.json` in the
decision13 directory. An earlier one-bounds harness used incorrect raster
width/height arguments and is superseded by this full tiled-function replay.

Target-specific float probes expose the failure inside the SPU raster path.
For rejected consumer frame 4273, a clipped face contains projected xyz/depth
triplets approximately (0.610,1,FLT_MAX), (0.724,-1,FLT_MAX),
(-1,-1,1.372), and (-1,1,1.303). The depth convention is reciprocal-like;
the projection code deliberately uses the maximum finite host float when
the relevant depth denominator is nonpositive. The lifted raster code then
performs operations such as `256 * FLT_MAX`, producing host infinity, followed
by other operations producing NaNs. With the original helpers these triangles
return no visible samples even against zero depth.

The architectural requirement differs from those host IEEE operations.
Section 9.1 of the [SPU ISA v1.2, printed pages 195–196](https://arcb.csc.ncsu.edu/~mueller/cluster/ps3/SDK3.0/docs/arch/SPU_ISA_v1.2_27Jan2007_pub.pdf)
specifies extended-range single precision, finite maximum bit pattern
0x7FFFFFFF, saturation on overflow, and no NaN or infinity results. The SDK's
`spu_fm`, `spu_fma`, `spu_fms` and `spu_fnms` currently use ordinary host
float arithmetic, which demonstrably violates that overflow behavior in this
captured model-filter path. This supports a runtime arithmetic correction,
rather than a renderer-distance or job-scheduling workaround.

As an isolated diagnostic, the replay clamps infinite arithmetic results to
signed host FLT_MAX before subsequent operations. All 43 speaker cases then
pass, including all eight formerly rejected cases. Comparing the complete
lists, these eight records add only `0x4ACE4000`; one later record also adds
unidentified model `0x4ACE4FA0`. No recorded accepted model is removed.
The comparison is `spu-clamp-overflow-comparison.json`. This experiment is not
an architectural fix: host FLT_MAX differs from the SPU extended maximum,
and treating extended operands, rounding, underflow, comparisons and
conversions needs consistent implementation and regression coverage. It has
not been applied to the game or SDK. The preserved executable's hash remains
`EF9CF6AB69EB984E6F22805C003608220CD45E78ECA126B2C5BDAFF7F7B06B7B`.

## Extended-range arithmetic prototype

A second, local arithmetic experiment decodes exponent-255 single-precision
operands as finite SPU values, calculates exceptional arithmetic with double
intermediates, truncates the packed significand, and saturates overflow at
the SPU maximum `0x7FFFFFFF`. It also handles extended operands in the
comparisons, signed conversion, and interpolation used by this program.
Ordinary host arithmetic retains its existing behavior in this prototype;
this is not a complete implementation of SPU rounding or underflow semantics.

Replaying all 43 decision13 inputs with this experiment accepts the speaker
in every case. Complete-list comparisons add `0x4ACE4000` in precisely the
eight formerly rejected records and add unidentified `0x4ACE4FA0` in record
25. No formerly accepted identifier is removed. Results are in
`local/visibility-conversation-decision13-decisions/spu-extended-overflow-comparison.json`.

The isolated live candidate replaces only the compiled occlusion SPU image39
object and includes the existing read-only PPU decision recorder. The SDK
and generated game sources remain unchanged. Its executable is
`local/candidates/visibility-spu-overflow/HazeRecomp.exe`, SHA256
`6B7E9DC67F64AA0C50B33B0F5DA49B75CAF4DC2A78B787F3C6478B232448C220`.
The original generated C declarations and definitions retain C linkage in
the separately compiled C++ prototype translation unit.

Run `20261012-202844-863` tests this candidate with label
`visibility-conversation-spu16`. Read-only memory checks before the approach
and after capture verify job11 mode `2`, module `0xF2C100`, and global occlusion
enable `1`. No job-mode or occlusion-enable writes are made. Final approach
ends with neutral input at QPC `1349140.4826581`; the conversation subsequently
runs without further player or camera input. The close-up observer opens the
gate at `1349151.9817706` and closes it at `1349169.960054`.

All 40 consecutive native images, frames 5006 through 5045, show the speaker
continuously through the close-up and pullback. The background airship also
remains visible, which alone does not establish its previously observed
disappearance's cause. All 40 corresponding model-filter records retain
speaker `0x4ACE4000`. Other models are still filtered: raw identifier
`0x4ACE3920` is removed in 26 records, and `0x4ACE3F60` in four.

Replaying these new candidate inputs provides a direct arithmetic control.
The original helpers reject the speaker in nine decisions (consumer frames
5009-5012 and 5014-5018). The extended-range prototype accepts it in all 40
and reproduces every complete recorded accepted identifier list exactly.
The comparison is
`local/visibility-conversation-spu16-decisions/spu-live-candidate-replay-comparison.json`.
This controls for differences in the live camera or captured depth data;
the same recorded inputs reproduce the failure with the original arithmetic.

The game closes normally with exit code 0 and 6,943 successful presents.
All four previously associated speaker mesh fingerprints occur in every
captured frame. The gated draw trace contains 45,568 execute outcomes and
no rejection outcomes; native frame N and N+1 renderer-mode checks have no
errors. Renderer packet/group ledgers remain balanced, rejection counters
remain zero, and `probe_skip_sampling` remains zero. The observer completes
its 180-second recording (5,401 samples) before the game is closed.
The preserved baseline executable retains its original SHA256.

## Airship model association and matching SPU rejection

Run `20261012-203633-397`, label `visibility-conversation-airship17`, uses
the unchanged decision-trace candidate with the original arithmetic. It
records the first conversation with normal job11 mode `2` and global
occlusion enable `1`, verified before approach. No dispatch-mode or
occlusion-enable writes are made. Final approach is followed by neutral
input at QPC `1349786.6549859`; there is no subsequent player/camera input.
The observer opens the gate at `1349797.4574947` and closes it at
`1349819.710037`. Forty native frames, 8385 through 8424, and 42 model-filter
records cover the close-up and pullback.

The airship disappears in native frames 8393, 8398, and 8403, returning in
the immediately following frame each time. Frame 8403 is particularly clear:
the two nearby soldiers remain visible while the entire parked airship and
its attached passenger structure vanish. Local full-size comparisons are
`visibility-airship17-disappearance-8393.png`, `-8398.png`, and `-8403.png`.
This reproduces the previously observed independent airship disappearance.

Geometry selected by source count 6,822 and attribute-8 fingerprint
`12c22404fd6d4912` is now spatially identified. Its three captures, at draw
frames 8386-8388, each contain 3,109 decoded vertices. Their shader divides
packed position xyz by w before applying vertex constants 0-3. Projection
through the captured constants and viewport adjustment places the points on
the airship's attached passenger structure in the same-numbered native
images. All 3,109 vertices project inside the viewport in each capture.
The overlays are `visibility-airship17-geometry-overlay-1.png` through `-3.png`.

Reconstructing world positions with the contemporary recorded view matrix
places all captured vertices inside model `0x4ACEB6C0`'s bounds. For trace 1,
the reconstructed range is approximately [-33.39,1.66,-11.08] through
[-29.99,5.80,-7.69]. The model's recorded full bounds are
[-56.0732,0.9104,-33.8121] through [-19.6188,8.6763,2.6764], covering the
whole airship assembly. The projection and bounds checks are saved as
`local/visibility-conversation-airship17-geometry/airship-projection.json`.
The small inter-frame differences in reconstructed positions are not used
to claim exact per-vertex world transforms.

The guest filter removes `0x4ACEB6C0` in consumer frames 8392, 8397, and 8402,
matching the three missing-airship native frames one frame later. Its
identified 6,822-source batch is also absent from the matched draw CSV at
8393, 8398, and 8403 and returns at 8394, 8399, and 8404. There are seven
recorded worker-list entries for the airship: the initial three reject it,
and the later four accept it. Do not interpret frames where it is not in
the worker input list as a rejection.

The unmodified full tiled SPU replay matches every complete accepted list
in all 42 records. Each of the three airship rejections persists with the
software occluder depth buffer cleared to zero. Target-specific probes
show the same overflow as the speaker case: operations involving projected
FLT_MAX depths produce infinities, including `-256 * FLT_MAX` at raster
function 0x4518, followed by invalid arithmetic.

The extended-range prototype accepts all three rejected airship cases and
preserves the four accepted cases. Across all 42 recorded lists, it adds
only the rejected speaker in ten records and the rejected airship in three
records, with overlap in two records. No previously accepted identifier is
removed. Comparisons are `spu-airship-replay-comparison.json`,
`spu-replay-comparison.json`, and `spu-extended-overflow-comparison.json` in
`local/visibility-conversation-airship17-decisions`.

This run closes normally with exit code 0 and 10,076 successful presents.
The draw gate contains 48,640 execute outcomes and no rejection outcomes.
Native N and N+1 mode checks have no errors; renderer packet/group ledgers
balance, rejection counters remain zero, and diagnostic sampling stays off.
Thus both reported disappearance types are traced to the guest's occlusion
filter using incorrectly emulated SPU single-precision arithmetic, rather
than distance selection or renderer packet rejection. The local prototype
is validated live for the speaker and offline against captured airship inputs;
this does not claim a live airship correction comparison or a complete
architectural floating-point implementation.

## Implementation follow-up

The user has been asked which executable/launch command exhibits the issue.
This can distinguish the preserved baseline and older default build from the
main checkout's changing performance candidates. The scene phase is now known:
approaching the soldiers and starting the first conversation. The immediate
speaker and airship model rejections, deterministic full SPU replay, and
arithmetic-overflow mechanism are established. The requested investigation
on a new branch is complete. A production fix should consistently implement
SPU extended-range arithmetic, add appropriate helper regressions, and receive
broader live validation. No permanent runtime correction is committed here.
The separate whole-scene black intervals are not explained by these
model-specific results.
