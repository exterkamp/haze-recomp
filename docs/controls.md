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

## Verification checkpoint

- `scripts/test-input.ps1 -SharedRoot E:\Code\haze-recomp -SDL3Root external/SDL3-3.4.18`:
  keyboard/raw-mouse/camera regressions; focus and coexistence; actual INI reload;
  real SDL virtual-device hotplug/reconnect, positional buttons, stick deadzones,
  analog triggers, controller rebinds/inversion, and a previously unrecognized
  flight-stick device enabled by a user mapping file all passed.
- Native build with main's pinned SDK completed at one compiler job. Six parallel
  jobs exhausted memory in the existing large generated PPU translation units.
- A 45-second native boot smoke run initialized SDL and presented over 1,280
  frames (`local/runs/20261012-220727-650`). Captured early frames show the title
  splash. This is boot evidence, not completed interactive gameplay validation.
- The SDK replacement API passed WARP GPU readback for RGBA/alpha, cache reuse,
  same-frame revision refresh, original-BC3 fallback and descriptor eligibility.
  Existing texture, depth, decal, resolution and postprocess checks also passed.

## Prompt rendering work still required

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

The isolated SDK now exposes a bounded RGBA replacement callback plus a cheap
revision callback, preserving original dimensions and channel remapping. It
reuses ordinary texture uploads and retires replaced resources safely.
Next: compose this atlas from the current bindings and Kenney PNGs, identify
the original by complete content plus format/dimensions, invalidate its cached
resource on input-family/config/icon-file changes, and verify the rendered
keyboard and controller prompts and hot reload in the native game. The current
game executable still shows original prompts; the goal is not complete.
