# Correcting missing PPU wait instrumentation

The initial wait-logging attempt produced renderer timings but no PPU wait
files. `YZ_PERF_PROFILE` was attached to PPU sources in the SDK's CMake
directory, while the game executable compiles those sources in `port/`.
Source properties are directory scoped. The fix applies PPU definitions in
the owning directory and retains the SDK scope for SPU and RSX sources.
`tests/check_profile_configuration.py` checks the actual five Ninja compile
rules after configuration. Both enabled and ordinary configurations passed;
the ordinary feedback executable contains none of these profiling definitions.

Muted native run `20261012-052247-183` with the corrected profiling build
emitted per-thread CSV timelines and exited normally with code 0. Capture
`requested_010961.ppm` was visually checked on the carrier. The timelines
include both PPU syscalls and HLE calls. Within frames 10000 through 10500
(37.778 seconds), the main PPU thread recorded 13.485 seconds of unioned
service-call wall time. Its 330-microsecond polling calls and semaphore waits
are concrete leads for further correlation with worker completion. Guest
thread 8 recorded 36.902 seconds of unioned service-call wall time, including
the 800-microsecond polling loop and audio HLE calls. These durations include
scheduling delays and blocking; they do not measure CPU consumption or prove
that reducing a wait would improve throughput. Nested scopes are unioned per
host thread rather than added together. Calls below 200 microseconds are
omitted, so coverage is a lower bound.

The earlier `20261012-051358-191` off/on/off attempt had PPU instrumentation
compiled out throughout. Its frame measurements cannot establish logger
overhead; background CPU load also changed between windows. The corrected
run establishes working instrumentation, not a controlled performance gain.
No timer or worker-priority behavior changed in this correction. Raw traces,
captures and the local summary stay outside Git.
