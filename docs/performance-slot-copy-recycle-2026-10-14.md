# Native job payload copy and producer recycle measurements

Tracking: #38 (native memory), #35 (submission), #71 (stall attribution),
parent #32. These follow the producer profile in
[performance-producer-steady-2026-10-14.md](performance-producer-steady-2026-10-14.md).
Implementation and fixture results alone do not establish a frame-time saving.

## Native payload copy

The build-local PPU transform replaces only the call to `func_00C07078` at
LR `00682798` in `func_00682648`. It follows the existing job tracing transform
and verifies hashes of the original or exactly traced caller and copy helper.
The original generated sources stay untouched. The preceding zero-fill and
subsequent metadata/publication remain separate generated operations.

The experimental selector `HAZE_NATIVE_SLOT_COPY=ON` enables a 5,136-byte native
`memmove`. It requires the exact slot-derived destination, length and LR;
ordinary bounded RAM; disabled incompatible memory diagnostics; and no reserved
destination line. The SDK native-write guard holds the reservation lock through
the copy. Any rejected call executes the original helper and trampoline. The
audited continuation replaces volatile arguments and recomputes the needed
condition/loop state; callee-saved state remains intact. This is a developer A/B
selector, not a new graphics setting or an enabled release default.

The differential fixture compiles the actual helper and SDK memory guard against
the extracted generated memmove and immediate caller continuation. It checks
overlap, destination reservations, ordinary-memory boundaries, diagnostic and
disabled fallback, unchanged rejected state and lock release. Both tests pass.
An enabled-only, idempotent summary reports attempts/native/rejected/bytes at
window close; no copy-path formatting or I/O occurs. Guest threads are not joined
by the current close path, so these independent atomic reads are a shutdown
snapshot, not guaranteed coherent final totals. Use them to establish execution,
not exact timed-window throughput.

## Producer FIFO recycling

SDK `7574134` adds a bounded `.recycle.csv` sidecar to the existing
`GCM_SLOW_DRAIN_CSV` diagnostic. It measures the callback and its two sequential
acknowledgements: consumption up to the current pointer, then wrapping to the
beginning. Initial/final observed addresses, expected addresses, wait counts,
thread identity and QPC brackets are retained. The synchronization contract is
unchanged. Records are buffered during gameplay and written at explicit shutdown.

The last 16,384 completed callbacks are retained in completion order. Metadata
reports overwrite counts and started/completed/in-flight callbacks. A still
blocked callback has no completed event; nonzero in-flight metadata therefore
means incomplete coverage. Correlation must use QPC intervals, not the renderer
frame counter sampled at callback entry. Disabled tracing does not query clocks
or renderer frames. An overlong sidecar path is reported explicitly.

The compiled source fixture passes disabled-path, delayed two-stage ack,
early-return, one-shot dump, overflow/order and path-length checks. The existing
FIFO slow-drain/kick fixture also passes all 15 cases. These are targeted checks,
not a claim that the whole repository test suite has run.

## Remaining validation

- Build, live native-copy use, recycle attribution and the initial ordinary A/B
  are completed below. The A/B does not justify enabling the native path.
- Establish the copy's direct operation cost or repeatable same-state benefit
  before revisiting promotion.
- Verify consistent pacing and unlocked/VSync behavior across gameplay; 144 FPS
  remains unachieved.

## First live diagnostic

Ordinary RelWithDebInfo build completed with exit code zero and at most two
compiler jobs. Candidate `local/candidates/slot-copy-recycle/HazeRecomp.exe`
uses Haze `e2ae549`, SDK `7574134`, SHA256
`1613CC66D7732B9EF9711D601A2FA356A9AC9DCBB656C81E9E586500C0AD5718`.
The compile-rule verifier confirms ordinary profiling/inline-read definitions.

Muted private-save run `20261014-011756-690`, PID 41192, enables the native copy
and only the FIFO diagnostic. The render settings match the previous jungle
capture: 2560x1440, 8192 shadows, FXAA/AF16, uncapped and VSync off. Other
experimental memory/job-delay paths and milestone/detailed draw traces remain
off. After Nectar and the transient second hint clear, setup captures show the
low left gauge and expected jungle view. Input-file detachment is acknowledged
before two 30-second absolute camera sweeps (tokens 3680/3681).

All 5,687 present intervals average **94.817 FPS / 10.547 ms**, p95 13.229 ms,
p99 15.500 ms, worst 20.743 ms; none exceeds 33.333 ms. Individual windows are
94.802 and 94.832 FPS. Pump means: command wait 5.557 ms, FIFO 3.137 ms, present
1.790 ms, other 0.063 ms. Camera/mode/present and process checks pass. Fifteen
resource samples bracket measurement (max gap 5.345 seconds), memory 52.68-53.15%,
no other Haze or compiler observed. No owned build, screenshot or heavy offline
analysis overlaps timing. This diagnostic is not a matched native-copy speedup.

The enabled-only shutdown snapshot reports **885,342 native copies, zero
rejections, 4,547,116,512 bytes**. It establishes that the intended native path
executes in gameplay, not its cost or timed-window throughput.

The recycle sidecar retains all 5,129 completed callbacks without overwrite;
5,130 started and one remains in flight at dump, so complete process-lifetime
coverage is explicitly false. All retained completed events are on host TID
5520. The recording spans the measured windows, but the missing in-flight event
has no timestamp; do not silently count it as a zero-duration callback.

Clipping separately to the two measured windows retains **1,136 callbacks**,
total **1,709.650 ms**: **1,673.848 ms** awaiting consumption to the current
pointer, **31.522 ms** awaiting wrap acknowledgement, and about 4.280 ms other
callback work. This is **0.301 ms of recorded producer callback time per present
interval**, not an additive renderer cost or a guaranteed recoverable saving.
Do not include the five-second inter-window gap in these totals. The longest
completed callback in the windows is 4.731 ms. The three worst pump frames have
no overlap with any retained recycle callback; the worst is 20.743 ms with
15.688 ms command wait. Recycling is therefore a smaller observed lead here
and does not explain those worst frames. Preserve the in-flight coverage limit.

The game exits normally with code zero; all seven immutable baseline save
hashes match. Evidence prefix: `local/jungle-slot-copy-recycle-a1-*`, including
all present intervals, resources, per-window recycle reports, raw sidecar and
worst-frame QPC correlation. Next measure ordinary copy-off/on with tracing off.

## First ordinary-copy A/B: not promoted

Both runs use the exact same candidate above, the private baseline save/cache,
1440p configuration, fixed existing CPU/GPU modes and two 45-second camera
sweeps after the tutorial effects settle. Recycle/detailed-renderer/stack traces
are off, diagnostic input is detached, and every present interval is retained.
The run order is OFF then ON; simulation/NPC state is not replayed exactly.

| Native slot copy | Frames | FPS | Mean ms | Median ms | p95 ms | p99 ms | Worst ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| OFF | 8,598 | 95.559 | 10.465 | 10.206 | 13.154 | 15.097 | 22.454 |
| ON | 7,815 | 86.861 | 11.513 | 11.289 | 14.591 | 16.584 | 22.853 |

There are no frames above 33.333 ms in either pair of windows. Copy ON's mean
is **1.048 ms higher**, not an improvement. The shutdown snapshot confirms
856,700 native copies, no rejections, 4,400,011,200 bytes; OFF has no copy
summary. The producer-wait/FIFO/present means are OFF 5.479/3.114/1.806 ms,
ON 5.872/3.679/1.895 ms. Changes span multiple phases, so this pair does not
isolate a causal cost to the copy helper. It does fail promotion: keep the
experimental native path disabled and do not call its implementation a speedup.

Both captures pass camera/mode/present and sampled process checks. Each has
20 resource samples; maximum gaps are 5.355/5.369 seconds. Memory is
52.65-53.15% OFF and 52.63-53.14% ON; no competing Haze or compiler is observed.
No owned build, screenshot or heavy analysis overlaps either timed window.
Both runs exit normally with code zero; all seven immutable baseline save hashes
still match. Evidence:

- OFF: `local/jungle-slot-copy-off-a1-*`, run `20261014-012413-387`, PID 42176,
  camera tokens 3700/3701.
- ON: `local/jungle-slot-copy-on-a1-*`, run `20261014-012945-888`, PID 14232,
  camera tokens 3720/3721.

Audit found one shared launcher limitation: `scripts/run.ps1` enables
`HAZE_BOOT_REPORT` unconditionally in these captures (also in the earlier
diagnostic). That background thread prints guest registers and flushes stdout
and stderr every five seconds. Both sides retain this identical condition;
neither is a fully logging-free release-like measurement. No timing evidence
here establishes the reporter's cost. The runner now makes it explicit through
`-BootReport`, clears inherited enablement by default and records the effective
choice. This launcher change has not yet been measured live.

The next copy-specific evidence should measure the original/native operation
cost directly or use a repeatable same-state comparison before another
promotion attempt. Merely repeating noisy whole-game runs cannot establish
which operation caused the difference. The larger unresolved target remains
main-thread command production/job completion and the inconsistent frame tail;
the recorded recycle cost is too small to explain that tail by itself.
