# Deferred renderer diagnostics

Tracking #71, #9 and #32. The stall-trigger capture in
`performance-pump-stack-timeline-2026-10-13.md` found three CRT logging-lock
wait samples inside a 104.920 ms pump interval, including 95.736 ms in
presentation. Periodic renderer counters were printed every 32 frames on that
thread. This identifies a synchronous logging path, not the lock's owner or
the full cause of that interval.

SDK commits `8ad73a5` and `b7c14f0` move routine `[live-draw]` counter output
to explicit exit dumps and renderer teardown. Periodic counter and `[fps]`
lines remain available under the existing `RSX_LIVE_FEED_DBG` diagnostic mode;
the ordinary default emits neither during gameplay. Counter collection,
renderer behavior, errors, and independent on-screen FPS calculation remain
in place. No new user-facing setting or tuning option is introduced.

The exit-dump call matters: Haze normally invokes
`rsx_live_draw_dump_present_samples()` and then `exit()`, without calling
`rsx_live_draw_shutdown()`. Review caught this after the first build. The
correction reports counters from that existing renderer-thread exit path as
well. This does not add asynchronous logging or remove other synchronous
diagnostics and control-file access; those remain part of #71.

The corrected ordinary profiling-off/FMA build succeeds with two compiler
jobs. Candidate `local/candidates/deferred-renderer-log-final/HazeRecomp.exe`,
SHA256 `0A439B8A23E1B5752B87E16B92F2AC986D66225F74CC1893432C0EB6F04CBAE5`.
Build log: `local/deferred-renderer-log-final-build.log`. Control remains the
32-slot ordinary candidate, SHA256
`37F8F02193F0CACEC97D6AF0D7D2D8BEB8BBEC79756891AF6FA40163286FDB59`.

## Preliminary exit-path failure

Run `20261013-125440-514`, prefix `local/jungle-logdefer-a1`, tested the
initial SDK `8ad73a5` build (SHA256
`4618C3792D2545FE4552906DF8AAFAC160B9F2CE4158E2147051E867712D582E`).
It correctly suppresses periodic output, but normal close lacks the intended
final counter report. Its raw evidence is preserved separately from the
corrected-build comparison. Two 24-second camera sweeps contain 3283 intervals:
14.4902 ms mean / 69.0121 FPS, p95 17.9128 ms, p99 23.9545 ms, maximum
324.7166 ms, ten frames over 50 ms. All camera/mode/present/process checks pass;
memory is 48.46-48.97%, no compilers observed, maximum resource gap 5.373 seconds.

The 324.7165 ms pump interval contains 187.5678 ms FIFO, 133.3735 ms outer
wait, and 1.9635 ms presentation. Another 276.4897 ms interval contains
234.9015 ms FIFO. Large stalls clearly remain after removing these particular
periodic reports. This preliminary run is not discarded as an outlier or used
to claim a speedup; it fails the exit-report requirement and uses a different
binary from the final planned candidate/control/control/candidate comparison.
