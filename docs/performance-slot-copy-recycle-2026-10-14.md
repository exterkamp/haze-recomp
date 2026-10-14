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

- Build the integrated ordinary executable with at most two compiler jobs.
- Verify the native copy executes during the private jungle save run.
- Measure recycle acknowledgement intervals and retain coverage limitations.
- Compare ordinary copy-off/on camera sweeps before choosing a release default.
- Verify consistent pacing and unlocked/VSync behavior across gameplay; 144 FPS
  remains unachieved.
