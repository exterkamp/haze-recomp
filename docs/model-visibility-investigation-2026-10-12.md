# Nearby model visibility investigation (in progress)

The user reports entire soldiers and airships disappearing and reappearing,
especially up close, including while neither the player nor camera moves.
The user subsequently identified the trigger as walking up to the soldiers
and starting the first conversation.
The requested investigation is on a new branch in the existing worktree:
`investigate/model-visibility`, based on `9c48009`. Its independent SDK branch
is `investigate/haze-model-visibility`, based on `9058dc4`. This preserves the
previous shadow clear correction. No model visibility fix is established yet.
The first-conversation recording now contains an externally observed
visible / missing / visible soldier sequence, also reproduced in native
readbacks. A matching draw trace now identifies two candidate mesh batches
whose submission tracks the soldier's visibility. Their model identity and
the cause of their omission still require a causal comparison.

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

## Outstanding evidence

The user has been asked which executable/launch command exhibits the issue.
This can distinguish the preserved baseline and older default build from the
main checkout's changing performance candidates. The scene phase is now known:
approaching the soldiers and starting the first conversation. The next useful
comparison can now target the externally recorded soldier sequence during
the first conversation. Native images now reproduce it; the remaining evidence
is causal identification of the two matching mesh candidates, then the reason
their submissions disappear. A confirmed cause and comparison are required
before any fix or completion claim. The separate whole-scene black intervals are not a substitute for that
model-specific comparison.
