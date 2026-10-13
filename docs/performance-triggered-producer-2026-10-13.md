# Triggered jungle producer capture

Issue #71, with parent tracking in #9 and #32. This is attribution evidence,
not a performance improvement or release acceptance measurement.

## Read-only capture trigger

`scripts/watch_frame_progress.py` validates the exact executable path, holds
the process handle, records its creation time and resolves the four-byte
`g_frames_presented` PDB symbol before starting the observation clock. It reads
that counter every 6 ms without suspending threads or writing game memory.
After a minimum observation period, a counter that stops advancing for at
least 50 ms triggers capture; the watcher then waits for recovery and five
additional seconds. It has a hard observation deadline and reports setup,
process-exit and read failures explicitly. A detected interval is confirmed
against the game's present/pump timestamps before attributing a hitch.

Five state-machine tests and two native fixture tests pass. The native tests
use the existing profiler fixture executable/PDB, validate exact process and
symbol identity, and check continuous progress versus a deliberate 250 ms
stall. Both fixture processes exit normally. They require the prebuilt oracle
from `scripts/build_profiler.ps1 -Test`; they skip explicitly if unavailable.

## Capture and retained coverage

Candidate `local/candidates/producer-job-trace/HazeRecomp.exe`, SHA256
`DE57794FA5ED8F53B1EF8FE2B0F8FEE2A7E96EB24F85E7DDED893CA31F727A4B`,
was built with renderer/job profiling and SPU FMA, using two build jobs.
Root source was `2b2a235`, SDK `bfb98f8e5ef48c1b938b7bd2f42b9272c81baf85`.
The game was muted and used a fresh disposable copy of the real jungle save.
Settings: 1440p, 8192 shadows, uncapped, VSync off, CPU flags 13571,
fixed startup controls, worker reuse enabled. Gameplay and dismissed tutorial
were visually verified before observation. No builds, screenshots, stack
suspension or heavy offline analysis occurred inside the observation window.

Run `20261013-141551-484`, PID 48364. Camera token 2640 selects the automated
30-degree sweep. The watcher ran for 41.561 seconds with a 50-second deadline,
20-second minimum and 12.8051 ms maximum polling gap. It triggered at counter
78220 and observed recovery to 78221. The selected trailing 20-second diagnostic
window is QPC 14138168689139..14138368689139 at 10 MHz. The game then closed
normally; launch and capture helpers returned zero, flushing the bounded rings.

The job ring retains 2,097,152 consecutive events, tickets
18,716,928..20,814,079. There are zero missing tickets, dropped events,
incomplete scopes or missing parents. The maximum discarded end timestamp,
14137713038393, precedes the selected window by 45.565 seconds.
All 15,375 overlapping title waits were matched and partitioned, with no skips.
Camera, process identity and native mode audits pass. Five resource samples
bracket the window: memory 49.855-50.338%, zero compilers, maximum sample gap
5.335 seconds. All seven immutable baseline save hashes still match.

## Hitch result

The watcher's trigger falls inside pump frame 78221:

| Pump scope | Wall time |
| --- | ---: |
| Whole frame | 68.866 ms |
| Outer command kick waits: 59 waits, one with queued work | 61.632 ms |
| FIFO processing: 63 drains | 5.269 ms |
| Present | 1.879 ms |
| Other | 0.086 ms |

The one retained slow drain overlapping this frame takes 4.4385 ms, including
0.0002 ms acquiring the FIFO lock. The pump thread's 15.625 ms CPU accounting
is quantized and is not a precise breakdown of work.

The first **52.8838 ms** of this frame overlap no instrumented job scope on any
traced thread. This is an observed instrumentation gap, not proof that every
thread is idle. During the entire frame, 17 matched title waits occupy a union
of 6.7361 ms. Their partition includes 3.2724 ms of ABI execution and 0.032 ms
of worker startup. Across all recorded jobs, ABI execution occupies a union of
6.0229 ms. These cross-thread wall intervals overlap pump waits and must not be
added to the pump times or treated as removable CPU cost.

The long delay therefore is not explained by an instrumented SPU job running
for the whole hitch, or by FIFO-lock contention in this capture. Next, locate
the producer's execution or waits before job submission: capture timestamped
producer phases or low-overhead scheduler/CPU evidence across the initial
53 ms gap. Uninstrumented jobs, simulation, synchronization and scheduling
remain possibilities; this trace does not distinguish them. The earlier
460 ms single-drain stall also remains unexplained and did not recur here.

For context, the selected window contains 1,623 present intervals, averaging
12.315 ms / 81.20 FPS, p99 18.052 ms, maximum 68.864 ms, with one interval over
50 ms. Selection is conditioned on a hitch and the binary is instrumented;
these numbers are not an ordinary-build FPS claim. Instrumentation overhead
has not been measured with an on/off comparison. Stable 144 FPS and improved
release frame pacing remain open.

Private evidence is retained under `local/jungle-producer-triggered-*`:
trigger, windows, process/resource logs, job/FIFO/pump/present CSVs, validation,
wait-frame analysis and correlated hitch JSON. No game data or saves are
included in this document or committed with the tools.
