# 30 FPS ceiling — 2026-10-11

The user revised the performance target to a maximum of 30 FPS with dips
acceptable, referring to [this console video](https://www.youtube.com/watch?v=VncZexTHFQ8).
The video was not accessible through the research tool; this records the user's
requested target rather than claiming an independent analysis of its frames.

## Implementation

Haze's native runner defaults `RSX_MAX_FPS` to 30. The shared SDK defaults to
unrestricted operation for other titles. The renderer uses a reusable Windows
high-resolution waitable timer and QPC deadlines. A normal timer and then
`Sleep` provide fallbacks. Waiting yields the thread rather than busy-spinning.

Each deadline is at least 1/30 second after the previous successful present.
A frame already past that deadline presents immediately. The next deadline
follows the new successful present, so a stall does not trigger catch-up bursts
or another entire frame's wait. Movie and game presentation share the limiter.
Rendering, simulation, and guest clock callbacks remain active.

Software pacing uses DXGI sync interval zero to avoid rounding each wait up
to another refresh interval on displays whose refresh rate is not divisible
by 30. This does not enable the tearing present flag. See Microsoft's
[Present documentation](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-present)
and [high-resolution timer documentation](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createwaitabletimerexw).
Successful-present timestamps measure submission cadence, not an independent
display scanout measurement.

For uncapped performance comparisons, use `scripts/run.ps1 -Uncapped` or set
`RSX_MAX_FPS=0` before launching the executable. A nonzero explicit override can
select another limit through 240 FPS. The run report records the configured
limit. Profiling CSV adds `frame_limit_ms` to distinguish deliberate waits from
remaining performance overhead.

## Validation

The full normal-build regression suite passes with the measured specialized
vertex loops enabled. A real timer test verifies 32 intervals of at least
33.333400 ms on this clock, plus first-frame, disabled, missed-deadline and
timer-failure fallback behavior. The test accepts scheduler-induced delays;
they must never shorten the interval below the ceiling.

Validation also reproduced an existing input-test flake: `Sleep(10)` did not
always advance the `GetTickCount64` value used by the 4 ms input poll gate.
The test now waits for that clock to advance before each requested report.
Production input handling is unchanged, and the complete suite then passes.

## Native verification

Muted ordinary build, run `20261012-004545-491`, with no per-draw profiling,
job logging, or instruction sampling. The log confirms `max_fps=30`, a
high-resolution timer, and DXGI sync interval zero. Captures verified the
title screen (1442), carrier facing out onto the deck (4558), and a lighter
wall-facing view after real analog camera input (7042).

| View | Frames | FPS | Minimum interval ms | p95 ms | Intervals shorter than 1/30 s |
| --- | --- | ---: | ---: | ---: | ---: |
| Title screen | 1500–1700 | 29.689 | 33.506 | 34.061 | 0 / 201 |
| Busy carrier deck | 4800–5399 | 20.701 | 33.609 | 62.945 | 0 / 600 |
| Wall-facing carrier | 7300–7599 | 29.413 | 33.548 | 34.393 | 0 / 300 |

The lighter view checks operation at the ceiling while full simulation keeps
running; it is not a substitute for the busy carrier performance baseline.
Capture/readback operations and camera changes occurred outside these windows.
The run closed normally with exit code 0. This verifies the submission ceiling
and camera input, while substantial carrier CPU overhead remains. These are
separate runs from the vertex A/B, so the FPS differences do not isolate the
optimization or limiter's cost.

Across all 9,575 consecutive intervals in the run, none was shorter than
1/30 second; the shortest was 33.4746 ms. This includes menus, loading and
captures, and is a ceiling check rather than a combined gameplay FPS claim.

Profiling-build smoke `20261012-005249-107` also closed normally. Its last
300 title-screen frames, 1115–1414, had complete CSV records with the default
vertex mode 259. All recorded positive limiter waits, averaging 32.166 ms,
with mean frame time 33.800 ms. This confirms the intentional wait appears
separately in profiling output. The ordinary build was restored afterward.
