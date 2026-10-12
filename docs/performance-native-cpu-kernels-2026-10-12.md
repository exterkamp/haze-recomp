# Experimental PC CPU kernels

Branch `experiment/native-cpu-kernels` and SDK branch
`experiment/haze-native-kernels` contain this work. The native cursor replacement
defaults off; main retains the established execution path. This is a first
operation migrated to host data and arithmetic, not a replacement of the
entire engine or its memory system.

## Evidence and boundary

The refreshed muted image-40 diagnostic `20261012-163000-781`, ordinary candidate
SHA256 `2651123FB7C7C76DC3C328B751D16BF867BC7DC4D3B24E247BD691FAD61036F0`,
captured the carrier before and after sampling (008882 and 009095). An eight-second
sample observed 168 announced jobs, opened 151 job threads and collected 338 RIPs.
72 locations fall in `3450`, 27 in `35B4`, 38 in task-launch checks and 26 in
call draining. These are sampled locations, including runtime helpers; they are
not CPU-utilization shares or proof of the frame's critical path. Filtered job
logging and brief thread suspension perturb the run, so its FPS is not used as
an ordinary-build comparison. Normal shutdown returned zero.

The original `3450` operation updates a 24-byte cursor: add a float increment at
offset 4 to offset 0, increment index at 8, decrement remaining count at 12, and
add the float increment at 20 to offset 16. The translated leaf repeatedly
loads 128-bit chunks, rotates, shuffles and writes intermediate guest registers.

There are four direct calls, paired in blocks `47E0` and `4900`. The native
replacement is inserted at those paired caller boundaries. It converts each
record into ordinary host fields, performs two float additions and two integer
updates, and writes only the four changed fields. The PC operation itself has
no guest context, registers, LS wrapping or instruction dispatch. The adapter
retains the existing buffer format for callers that have not yet migrated.

The pair preserves ordering even for identical or overlapping records. Both
pointers must be valid, aligned, nonwrapping 24-byte local-store records before
any native write. Image, caller-block and LS ownership guards reject unsupported
contexts; active/uninitialized LS probes keep the original path. The original
leaf and both original call sequences remain as fallbacks. The complete image
hash and the leaf/caller hashes fail closed on changed generated input.

The caller continuations overwrite or discard the leaf's volatile r2..r37
results. The replacement preserves the link, stack, all other registers, branch
decision/target, memory and context metadata at the boundary; it deliberately
does not recreate discarded scratch results. The enclosing routine restores its
saved registers, and its direct outer caller supplies fresh r3/r4 to the next
operation. Broader gameplay validation is still required before default use.

`HAZE_NATIVE_CURSOR=1` enables it. `HAZE_NATIVE_CURSOR_FILE` names a diagnostic
0/1 file for same-process comparisons; mode changes are logged with QPC and a
first accepted-operation message verifies that the guards accepted actual work.
Only the diagnostic selector uses synchronization; it does not fabricate guest
job completion or alter DMA/queue signals.

## Correctness and CPU-cycle measurements

The oracle extracts the actual original leaf and both caller blocks locally.
4,200 comparisons cover both caller branches, all four word alignments, identical
and overlapping records, random float bit patterns, integer overflow and original
fallbacks for unaligned/out-of-range/wrapping pointers. Accepted cases compare all
memory, control transfer, context metadata and registers outside the audited
volatile set. Fallback cases compare the full context byte-for-byte. Guard mutation
checks require unchanged state. This is a boundary-contract comparison, not an
assertion that the deliberately discarded scratch registers remain identical.

Four one-million-pair CPU-cycle runs measured original 251.642..254.514 cycles
versus native 47.768..50.541 cycles (4.988..5.293x). The fixture's call-drain and
image-restore stubs check the leaf's simple return contract but omit real runtime
cost. These figures describe the extracted pair, not whole-game speed or an FPS
multiplier. The full candidate build and regression suite passed.

The SDK additionally removes four byte-order shuffles from the general SIMD
shuffle helper. It operates directly in host order and maps source selectors
through XOR 3, preserving selector aliases and 00/FF/80 special results. Tests
cover every selector at every output byte (4,096 cases), 200,000 random controls,
and the existing numeric/byte/LS suites. A standalone retained-legacy comparison
measured about 20.2..20.5 versus 16.2..16.6 cycles per dependent operation across
SSE and AVX/FMA builds. This is a local CPU saving, not a measured FPS gain.

Artifacts: `local/image40-refresh2-{run,samples}.json`,
`local/native-cursor-{oracle,build-tests}.log`, `local/cursor_oracle.c` (private
game-derived fixture), and `local/shufb-direct-{sse,avx}-cost.log`.

## Ordinary candidate gameplay validation

Candidate `local/candidates/native-cursor/HazeRecomp.exe`, SHA256
`55BB364CA52CD4D93D5ECEFCAE2FCDB5DD2A662A3EC84118DFF02D7670005EAF`,
passed the full build/regression suite. Muted run `20261012-165219-051` used
2560x1440 rendering, VSync on, host FPS limit zero, follow shadows, AA off and
AF 1. Native PPU queue and SPU idle waits were on; native triplet copying was
off. The build had no CPU/renderer/job profiling enabled.

Four 30-second windows with five-second warmups switched only the cursor mode.
There was no input, frame capture or sampling inside the measurement windows.
Mode events validate each window, and the accepted-operation event confirms
execution through the native replacement in the carrier. Captures 008372 and
013262 show the carrier before and after; normal shutdown returned zero.

| Cursor mode | FPS | Mean frame ms | p95 ms | p99 ms | Process mean cores |
| --- | ---: | ---: | ---: | ---: | ---: |
| Original | 20.751 | 48.191 | 65.090 | 89.788 | 3.918 |
| Native | 20.323 | 49.206 | 65.013 | 77.112 | 3.947 |
| Native | 21.532 | 46.443 | 62.141 | 79.425 | 3.932 |
| Original | 20.371 | 49.090 | 65.086 | 78.793 | 4.035 |

Another agent's Haze process (PID 31556, shadow-lighting worktree) started after
launch and was observed during measurement. The strict isolation check rejected
the run. The table is a contended diagnostic, not evidence of an FPS or pacing
improvement. The other process was left untouched. The kernel remains off by
default pending an isolated comparison and broader gameplay validation.

Artifacts: `local/native-cursor-abba-{run,windows}.json`,
`local/native-cursor-abba-presents.csv`,
`local/native-cursor-abba-processes.jsonl`, and
`local/native-cursor-abba-contended-report.json`. The latter deliberately omits
the strict observer check to expose the raw diagnostic numbers above; it must
not be presented as an isolated result.

## Rejected lead: floating-point denormal handling

The translated cursor's discarded SIMD lanes reinterpret small integer counters
as subnormal floats. To test whether those lanes explain its cost, the extracted
fixture explicitly cleared, then set, both FTZ/DAZ bits for four repetitions per
mode. Original cost was 230.8..233.2 cycles with the bits clear and
228.6..239.7 with them set; native cost was 31.9..43.9 across both modes. This
does not support denormal handling as the explanation for the original pair's
cost. The diagnostic restores MXCSR afterwards and never changes game FP mode.
Correctness checks precede these normal-float timing fixtures. The repeat also
illustrates variation in the microbenchmark's native cost; the speedup is local
to this operation, not a fixed whole-game multiplier.

Artifact: `local/native-cursor-denormal-cost-verified.log`.

Consistent frame pacing and broad unlocked-rate gameplay validation remain
unfinished; the full goal stays active.
