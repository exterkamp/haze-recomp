# Native controls

Worktree `input-prompts`, branch `feat/input-prompts`, starts at main `a872469`.
The default launcher mode is `Auto`; `-InputMode KeyboardMouse` and
`-InputMode Controller` restrict it. Direct runs use `HAZE_INPUT=auto`,
`keyboard`, or `controller`. SDL3 replaces port-zero input in both automatic
and controller mode, preventing duplicate XInput reports.

`config/input.ini` reloads within 500 ms of a save. `HAZE_INPUT_CONFIG` selects
another file. Keyboard bindings and raw mouse sensitivity/invert remain
available. `[ControllerBindings]` maps the game's pad buttons to physical
SDL positions (`south`, `east`, `west`, `north`), named shoulders/sticks/D-pad,
or an axis. `none` unbinds. Axis-as-button bindings use their positive half;
left/right triggers retain analog pressure for the game's L2/R2. Movement and
look axes, radial deadzones, trigger threshold, and look inversion are in
`[Controller]`. Unknown names log and disable the affected binding.

SDL handles Xbox, PlayStation, Nintendo, and other recognized controllers.
`MappingFile` accepts an SDL mapping database, relative to the INI. A custom
joystick mapping can describe arbitrary raw buttons, axes, and hats. Steam's
virtual controller output works through SDL; `IconFamily` allows explicit
Xbox/PlayStation/Switch selection when Steam hides the physical device family.

SDL is pinned to 3.4.18 with a source SHA-256 in
`port/controller-dependency.cmake`. A compatible installed SDL3 package or
`scripts/build.ps1 -SDL3Root path/to/SDL-source` supports offline builds.
Builds in an initialized worktree use its pinned SDK; `-SharedRoot` shares
game data/lifts, while `-SdkRoot` explicitly overrides the SDK.

Input callbacks read a synchronized controller snapshot; all SDL operations
run on the window thread. Focus loss neutralizes input. The last meaningful
key/button press or mouse/stick movement selects the prompt family. Releases,
held controller buttons, and sub-deadzone noise do not repeatedly take over.

The in-game controls/rebinding menu is tracked in
[issue #23](https://github.com/zdiemer/haze-recomp/issues/23).

## Verification

- `scripts/test-input.ps1 -SharedRoot E:\Code\haze-recomp -SDL3Root external/SDL3-3.4.18`:
  keyboard/raw-mouse/camera regressions; focus and coexistence; actual INI reload;
  real SDL virtual-device hotplug/reconnect, positional buttons, stick deadzones,
  analog triggers, controller rebinds/inversion, and a previously unrecognized
  flight-stick device enabled by a user mapping file all passed.
- Native build with SDK `8660186` (branch `feat/haze-input-prompts`, published
  to the SDK origin) completed at one compiler job. Six parallel
  jobs exhausted memory in the existing large generated PPU translation units.
- The 85-second native probe at **2560x1440**
  (`local/runs/20261012-230151-250`) captured the Select legend changing
  Space -> E after a live INI save, E -> Z after a PNG replacement preserving
  its timestamp, original PS3 Cross after disabling replacements, and Z after
  re-enabling. A real E key press in the owned window advanced the menu with
  the new binding. Frame headers confirm 2560x1440; the preview window was
  1280x720. `local/native-prompts-report.json` maps each stage to its frame;
  `local/prompts-verification.png` contains crops at their captured pixel size.
  This verifies menu behavior and GPU hot reload; full campaign play and real
  controller hardware have not been exercised in this worktree.
- The SDK replacement API passed WARP GPU readback for RGBA/alpha, cache reuse,
  same-frame revision refresh, original-BC3 fallback and descriptor eligibility.
  Existing texture, depth, decal, resolution and postprocess checks also passed.

## Prompt rendering

The Kenney Input Prompts archive in Downloads contains version 1.5A. The 363
default keyboard/mouse, Xbox, PlayStation and Switch PNGs are copied unchanged
to `assets/input-prompts`, with license, per-file hashes and archive provenance.
`scripts/import-input-prompts.py` reproduces the import.

The game's shared `misctex/buttons/padbuttons` atlas has been identified in the
local retail data. It is 384x240 BC3, 92,160 source bytes, with 6 columns and 4
rows. FNV-1a64 of the original compressed pixels is `1679f0984072814b`;
SHA-256 is `41ecf9f474957e61e927744373166826d80afd8af3323d6e0199eef8b808d570`.
The archive entry hash is `d607ed02`. Extracted data and previews remain local.

Row 0: left stick, right stick, left vertical, right vertical, L2, R2.
Row 1: left horizontal, Select, Start, right horizontal, L1, R1.
Row 2: Cross, Square, Circle, Triangle, L3, R3.
Row 3: Left, Right, Up, Down, system button, D-pad.

`port/haze_prompts.cpp` uses Windows Imaging Component to decode the PNGs and
compose a replacement at the original extent. Keyboard/mouse glyphs use the
current `[Bindings]`; controller glyphs use `[ControllerBindings]` and the
selected family's physical labels (Nintendo south is B). Movement uses the
four current keyboard movement keys. Unknown/None bindings have empty glyphs.
The exact original compressed-pixel fingerprint, dimensions and BC3 format
identify the target; unrelated textures retain their original decoder.

`[Prompts] Enabled=0` restores original game prompts. `Directory` defaults to
`../assets/input-prompts`, resolved relative to the INI. Family changes rebuild
immediately; the INI and all PNGs used by the active family are checked every
500 ms, including content checks for copies that preserve timestamps.
Missing or malformed used PNGs restore the whole original atlas until
the pack is repaired. WIC decoding stays on the window thread; immutable atlas
snapshots are shared safely with the renderer. Alpha-aware bilinear resizing
keeps transparent icon edges clean without nearest-neighbor jaggies.

The isolated SDK exposes a bounded RGBA replacement callback plus a cheap
revision callback, preserving original dimensions and channel remapping.
Changing a revision refreshes an already cached GPU texture, even within the
same frame. Normal texture uploads and safe resource retirement are reused.
Installation includes the INI and CC0 icons beside the executable.

The prompt tests cover real PNG decoding for all families, binding changes,
unchanged cache revisions, icon-file edits, disabled prompts, malformed-pack
fallback and recovery. An optional untracked retail fixture verifies that a
one-byte change prevents replacement. Physical controller hardware has not
been exercised; the SDL tests use its real virtual joystick driver.
