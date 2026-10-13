# Opening-logo skipping

`config/input.ini` supports a `[Startup]` section:

```ini
[Startup]
AutoSkipLogos=1
AutoSkipAttract=1
```

Both default to `1` when omitted. Only `0` and `1` are accepted; invalid values
report a diagnostic and use the safe automatic default. Changes reload through
the existing input configuration path within half a second. The settings work
in the executable itself, with keyboard, controller, and automatic input modes;
they do not depend on `scripts/play.ps1`. `HAZE_INPUT_CONFIG` selects the file
for direct launches, as it does for other input settings.

`AutoSkipLogos=0` waits for the current Cross/confirm binding. Defaults are Space,
Xbox A, PlayStation Cross, and Switch B. The movie hint follows the active input
family and the current binding. Mouse buttons work without camera capture.
Releasing skip after a movie ends is required before another menu accepts it,
including when the same physical control is bound to both Cross and Start.

**Movie playback is unavailable.** The pinned CellSail backend signals stream
completion without decoding frames. Disabling automatic skip cannot show the
publisher movies, and may leave a blank background until manual confirmation.
The hint says so. This preference does not implement a watch-logos mode.

Only the exact disc paths for `ubi.pam`, `ubi43.pam`, `frd.pam`, and `frd43.pam`
are opening branding. `attract.pam` has its own preference, enabled by default
because that stream is also undecoded. Campaign streams never acquire skip
ownership, and a new unrecognized stream cancels any previous ownership.
No saves, first-run initialization, or guest menu state are patched.

Automatic pulses start 300 ms after stream start and alternate 400 ms pressed /
300 ms released until CellSail STOP, CLOSE, destruction, or another stream start.
There is no ten-second expiry that strands slow loads. Manual confirmation is
accepted after the same start-completion grace period, including between auto
pulses. Automatic skipping works during an unfocused launch; manual input does
not. After the stream ends, all buttons are consumed until a focused neutral
report, so changing focus cannot release a held skip into the next menu.

The native hint is an owned, nonactivating window. It scales with the client
height, tracks window moves/resizing, and hides after playback or minimization.
It never takes focus or consumes clicks. Renderer-only frame dumps do not
include this host window.

## Verification

`scripts/test-input.ps1` exercises the actual input mapper plus SDL virtual
controllers and prompt rendering. Logo cases cover all four filenames,
independent attract policy, rebinding, shared Cross/Start bindings, mouse skip
without capture, controller-only manual skip, loss/regain of focus while held,
slow loading beyond ten seconds, and cancellation before story playback.

Native logs include `[startup] logo start/stop at +N ms` and separate `attract`
events, measured since input initialization. Stream duration is the matching
stop minus start timestamp; it is not total startup/loading time. A title-screen
capture is required to verify arrival at the title, rather than treating the
last logo stop as proof.

The browser path uses the same lifecycle filter and default automatic policies.
Its current browser UI does not expose these INI preferences or the native
hint. Browser execution and focus behavior require validation under #53; the
native test results do not establish browser acceptance. Executable-launcher
validation is pending availability of #42.

### Native evidence (2026-10-13 UTC)

Built from main `4adab80` and its unchanged pinned SDK `8660186`, with SPU FMA,
profiling off. Final executable SHA-256:
`6050504D1B9561C76658B2F7350D3855B7E16510594D0C40D6040B6E5D25C20E`.
The complete input suite passed (SDL virtual controller, prompt PNG rendering,
actual host mapper, native mouse camera), and the native executable rebuilt.
Logs are in the parent `.worktrees/menu-logos-input-tests-final.log` and
`.worktrees/menu-logos-build-final.log` (local only).

All native probes used 2560x1440 rendering and a borderless 2560x1440 client,
muted audio, fresh processes, and the existing shared shader cache. These are
not empty-cache startup measurements or physical-controller tests.

| Local run under `local/runs/` | Observation |
| --- | --- |
| `20261013-060946-723` | Keyboard mode, automatic skipping: first logo start 19.406 s after input initialization, stop 20.203 s; second start 20.531 s, stop 21.313 s. Title visible in frame 630 at approximately 25 s after launch. |
| `20261013-061122-439` | Controller input mode, automatic skipping without a physical pad: first logo 18.656–19.453 s, second 19.781–20.578 s. Title captures at approximately 26/28 s after launch. |
| `20261013-061219-179` | Automatic logos disabled: first logo started at 18.437 s and did not stop during the 65 s probe. Windows refused foreground activation, so no manual key was sent. |
| `logo-policy-final` | Final candidate, direct executable launch with explicit paths: first logo held at 27.097 s, hint captured and visually inspected; enabling the preference in the same INI skipped both logos. Title captured at 36.067 s, visually inspected; owned window closed with exit code 0. |

STOP, CLOSE and descriptor destruction can each log a stop; durations above use
the first stop for a start. Roughly 19–20 s precedes the first logo; the two
automatic skip waits themselves are about 0.8 s each. Remaining time includes
the game's static legal screens and menu loading, which this change preserves.
The bounded probes did not advance past Press Start without input.

The unit mapper verifies manual keyboard/controller/mouse skipping and release
gating. Real foreground keyboard input, physical-controller hotplug, and browser
execution remain interactive checks; the refused foreground activation is not
counted as a pass. The host hint was captured independently with `PrintWindow`
because renderer frame dumps exclude owned Windows UI.
