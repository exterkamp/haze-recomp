# Native vertex-program packet consumption

Tracked under [#35](https://github.com/zdiemer/haze-recomp/issues/35).

The FIFO consumer previously decoded the title's 32-word vertex-program upload
one method at a time, including repeated guest reads and calls into the live,
optional dispatch, and legacy state mirrors. A new bounded route handles the
existing exact `0x00800B80` packet as one operation. It keeps the original
command bytes and consumer ordering; there is no producer-side queue or early
submission from a guest thread. It defaults off pending runtime comparison.

The boundary requires an aligned GET, all 132 bytes published before PUT,
ordinary-RAM mappings for every payload word within VM bounds, and a cursor
that cannot wrap. It validates every mapping before reading payload or updating
any mirror, including packets crossing noncontiguous IO pages. It then reads
32 host-endian numeric words through the original `vm_read32` function and
updates live, optional dispatch, then legacy state. GET advances by 132 once.
Other packets, incomplete packets and rejected mappings use the original path.

The kernels preserve register values and seen counters, instruction-upload
cursor increments, the legacy little-endian microcode representation, high
water/dirty flags, and all unrelated state. The generic legacy handler's
capacity test is also hardened to avoid unsigned `w+4` overflow on malformed
cursors, so fallback cannot index outside the microcode array in that case.

The live wrapper retains per-method handling during movie mode, composite UI
or debug rendering. The normal ready path calls the kernel once. Thus batching
does not change the existing serialized snapshot boundary. Ordinary rendering
already bypasses those locks/atomics; they are not a claimed saving. The
optional draw engine retains its ready check. Non-Windows live stubs remain.

Per-word diagnostics force the original path: four consumer trace selectors
and the 15 memory-probe selectors are checked individually, along with a
`VM_SAMPLE_READS` build. Packet-level histogram handling stays before either
route. `RSX_VP_BURST` / `RSX_VP_BURST_FILE` are internal experiment controls,
not graphics settings. Events and counts show which route actually executes.
`RSX_VP_BURST_PROFILE` optionally measures eligible packet routes in a separate
diagnostic run; ordinary FPS comparisons leave it absent.

The full `build.ps1 -SpuFma -Test` gate passes after updating the FIFO test
fixture to supply its real VM allocation size. Kernel tests link the actual
generic handlers and pass 2,200 complete dispatch-state comparisons, 16,432
complete legacy-state comparisons and four native-rejection/generic-safe
overflow cases. Another 62 checks extract the actual FIFO boundary and live
wrappers: all 32 mapping failures reject without reads or mutation, true IO
page crossings work, invalid headers/publication/bounds reject, backend
enables and ready/movie/debug combinations retain state and route behavior,
and each diagnostic flag independently disables batching. Mode-report tests
also pass. These tests do not establish full-campaign correctness or FPS gain.

Candidate `local/candidates/native-vp-consumer/HazeRecomp.exe` has SHA256
`1B5AF9B98069373B750BAA0435CC959CB6AD76E20F841246D795CAD83FC52A61`.
The producer-side native upload remains disabled during these comparisons;
GPU occurrence mode 2 and native index preparation remain fixed.

## Ordinary gameplay comparison

Run `20261013-031731-227`, private evidence prefix
`local/native-vp-consumer-motion`, used 2560x1440 rendering, a 1280x720 window,
VSync on and no frame cap. Shadows followed render resolution, AA was off and
AF was 1. Geometry/vertex caches, vertex workers and native job wait were off.
Audio ran muted. The initial carrier scene was visually checked before four
24-second measurements, each after five seconds of warmup. The camera followed
the same absolute +/-30 degree yaw sweep with a four-second period.

| Route | Frames | FPS | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|---:|
| Original | 605 | 25.27 | 39.57 | 54.84 | 61.01 | 72.21 |
| Native | 598 | 24.95 | 40.07 | 55.03 | 67.21 | 98.77 |
| Native | 618 | 25.79 | 38.77 | 52.44 | 59.87 | 112.50 |
| Original | 497 | 20.74 | 48.21 | 74.17 | 97.03 | 201.55 |

All four windows pass complete present-ring coverage, mode-event, actual-camera
and process-bracketing checks. Only the owned Haze process was observed, but
Labyrinthine was running concurrently and was foreground in observer samples.
These are shared-load measurements, not an isolated baseline. The large drift
between original-route blocks prevents attributing a pooled improvement to
batching. **This run does not establish an FPS gain.**

Before measurement, the native route had accepted 1,349,967 packets with zero
fallbacks. The owned process exited normally. Two independent source reviews
found no current state-parity blocker. A theoretical movie-mode transition
mid-packet would be observed only once by the ordinary wrapper; there are no
current call sites of that setter in this tree. Campaign coverage remains open.

## Separate route timing

Run `20261013-032420-263`, prefix `local/native-vp-consumer-profile`, used the
same binary/settings with `RSX_VP_BURST_PROFILE=1` and alternating modes. It
passed the same present/mode/camera/process checks and exited normally. The
following completed mode segments include warmup (about 29 seconds each):

| Route | Presents | Packets | Microseconds/packet | Route ms/present |
|---|---:|---:|---:|---:|
| Original | 620 | 1,009,007 | 0.581 | 0.945 |
| Native | 625 | 1,004,483 | 0.154 | 0.248 |
| Original | 610 | 967,955 | 0.585 | 0.928 |
| Native | 648 | 1,023,232 | 0.149 | 0.235 |

Both enabled segments used only the native route, with zero fallbacks. The
route elapsed time fell by approximately 0.69-0.70 ms per presented frame,
about 74%. Timing includes preflight, guest reads and all mirror updates on
the native route; the disabled route includes its original per-word loop.
Both exclude the subsequent GET advance and include QPC instrumentation cost.
The present CSV supplies QPC frequency (10 MHz). Only counters paired with
mode-transition timestamps are used; periodic cumulative snapshots are not
summed. The fallback counter includes diagnostic-forced fallback when enabled,
so it must not generally be interpreted as malformed-packet count.

This is aggregate route elapsed time normalized by presentations, not a
measurement of critical-path or whole-frame savings. It supports keeping the
experimental implementation, but does not meet the evidence required to
enable it by default. The roughly 39-40 ms ordinary enabled mean and long
frame tails remain far above the 6.94 ms target for 144 FPS. Further work needs
to attribute the main-thread/job-delivery stalls and remaining renderer work;
this bounded command upload operation cannot account for most of that gap.

SDK implementation: `8ea7b04` on `experiment/haze-gpu-geometry`.
