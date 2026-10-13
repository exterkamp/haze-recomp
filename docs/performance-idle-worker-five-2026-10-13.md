# Extending Haze idle-worker reuse to images 32 and 33

## Change and source audit

The previous complete phase capture identified dedicated-thread startup
delays around 20–24 ms for images 32/33 while the three already eligible
images had much shorter average startup gaps. See
`performance-idle-worker-phases-2026-10-13.md` for the measurements and
contention limitations. This change adds two exact whole-image allowlist
entries in `port/main.cpp`; it changes no SDK execution or rendering code.

| Image | Generated workload | Full fingerprint |
|---|---|---|
| 32 | `spu_0031_at_00ECEC00` | `D7E7384CB7BEF175` |
| 33 | `spu_0032_at_00EDC900` | `6840DFFCF7F63411` |

The registrations also have text aliases, which are not enabled. The SDK
requires the registered image ID and exact fingerprint to match; unknown
jobs, image 22, and other dedicated workloads retain their existing behavior.

Neither generated source has mutable module-level or thread-local state;
their file-scope objects are constant function tables. Entries initialize
architectural registers/local-store fields within a fresh `spu_context`.
Shared `spu_run_lifted_job_abi_impl` initializes the context and copies the
job's local store for each invocation. Task arguments are captured by value
before dispatch. Both paths call the same `spu_async_thread` callback, including
byte-operation TLS selection, stack headroom, MFC/coherency cleanup, taskset
completion and reservation cleanup before the worker becomes idle.

The halt driver resets host depth and the trampoline on entry, with nest-safe
halt-state restoration. The known functional thread-local `s_exit_seen`
exception is guarded by image 22, which is excluded. Other reviewed TLS is
diagnostic history/configuration or belongs to a different synchronous SPURS
job-chain API. `s_wait_exit_ms` affects diagnostic logging only. WAIT_SIGNAL
would retain a busy worker until the callback completes; the cache has no
queue or concurrency bound, so another dependent job can still start. No
assumption that every callback is nonblocking is needed for capacity progress.

This is a controlled experimental extension, not proof of broad gameplay
correctness. The existing 31 worker-cache checks cover callback ownership,
simultaneous submissions, overflow, nested dependency progress, failures and
retirement; they do not compare these two game jobs' output buffers. Live
acceptance must establish actual reuse of the exact fingerprints, completion,
continued gameplay, and frame timing. Whole-job output parity and transitions
remain separate validation work.

## Live diagnostic result

Run `20261013-104605-286`, PID 19340, used the copied jungle checkpoint,
1440p/8192 shadows, VSync off and no frame cap. Tutorial dismissed and Nectar
activated before two 24-second camera sweeps with five-second warmups, **0
then 1**, tokens 2140–2141. Audio muted; process exited normally after the
capture. Candidate `local/candidates/idle-worker-five-profile/HazeRecomp.exe`
SHA-256 `F71A883090172D0D376C08A7661BFAF3DA5003013701E9679973D297B84A480D`.
It contains the two title allowlist additions on root 15c0a34 / SDK 4bef1a5;
the separate slow-draw diagnostic being developed was not in this binary.

Logs confirm actual reused dispatches with full fingerprints
`image=32 fp=D7E7384CB7BEF175` and `image=33 fp=6840DFFCF7F63411`, along with
the three previously allowed images. Both added jobs continue executing,
finishing and cleaning up repeatedly during the enabled window:

| Image | Off startup mean / max ms | On startup mean / max ms | On reused / created dispatch spans | On finish / cleanup spans |
|---|---:|---:|---:|---:|
| 32 | 0.22753 / 15.0267 | 0.01324 / 3.3795 | 7696 / 0 | 7696 / 7696 |
| 33 | 0.62894 / 7.2716 | 0.04503 / 13.0607 | 7473 / 223 | 7696 / 7696 |

These are creation/reuse-call start to setup-start wall times, not CPU costs.
Counts select spans overlapping the window; they are not general proof that
every possible submission completes. Image 30 also has 1273 created dispatch
spans against 29936 reused spans: the 16-slot cache still has spillover under
load. Capacity expansion is a measured candidate, not part of this change.

The full ring is retained for both windows: 2,097,152 consecutive tickets
3169809–5266960, discarded maximum end QPC 14005250635465 before the first
window. Missing/dropped/incomplete spans and missing parents are all zero.
The wait-frame analyzer validates 19,929 and 19,948 overlapping waits, no
skipped joins, covering 1762 and 1922 complete pump frames.

| Mode | Present intervals | Mean ms | FPS | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|---:|
| All dedicated (0) | 1762 | 13.612 | 73.463 | 17.498 | 21.446 | 32.947 |
| Five eligible (1) | 1922 | 12.476 | 80.156 | 16.624 | 19.602 | 42.058 |

No intervals were removed. The enabled window has three gaps above 33.333 ms;
the control has none. Camera/mode/present/Haze-process audits pass. The
resource observer brackets both windows with 12 samples, 48.67–50.06% memory
and maximum gap 5.363 seconds. One observation found two compilers during a
sample spanning QPC 14005941639634–14005944682526; the second window ends at
14005942332454. **That sample straddles the window end**, so its end timestamp
alone cannot exclude compiler activity within the final portion of the window.
The exact compiler snapshot time was not recorded. No other Haze was observed.

This is an instrumented diagnostic, not an isolated ordinary-build gain and
not a direct three-versus-five-job comparison. Window order, scene evolution
and scheduling remain limitations. The timing evidence supports the intended
startup mechanism; it does not establish broad gameplay or consistent pacing.

In the slowest 1% of complete pump frames, matched startup overlap averages
3.375 ms off versus 0.331 ms on. The worst enabled pump interval (frame 35279,
42.059 ms) has only 0.121 ms matched startup but **33.681 ms ABI execution
overlap**, almost all image 30 (32.872 ms). That ABI wall span includes any
waiting/descheduling within execution; it is not measured CPU work. Another
33.749 ms frame has 17.910 ms image-30 ABI overlap. This shifts the next
long-tail investigation toward execution/dependency/scheduling inside jobs,
alongside the independent unmeasured renderer wall time. It does not explain
the earlier multi-second stall, which did not recur in these windows.

Private evidence prefix `local/jungle-idle-worker-five`: trace, windows,
report, pooled, pump, renderer, presents, camera, process/pressure observers,
wait-frames and async-images results. Build log:
`local/idle-worker-five-profile-build.log`. No new user setting was introduced;
the shipped b1ca506 WIP remains unchanged pending ordinary-build comparison.

Tracking #36, #32 and #9 remain open. Next validation: ordinary three-image
baseline versus five-image candidate from the same checkpoint, repeated
ordering, full intervals and resource observation, then transitions and
broader playability. Do not promote a diagnostic result to a stable-FPS claim.
