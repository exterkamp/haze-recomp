# REF publication is inactive in this jungle capture

Tracking: #35, #71 and #32. This is a rejected bottleneck lead for the observed
checkpoint, not a synchronization replacement or an FPS improvement.

## Hypothesis and observation method

The SDK's `cellGcm_ref_on_poll` leaves each queued reference value observable
for about 200 microseconds. Four generated Haze helpers (`006F0208`, `007185D8`,
`0071B738`, `00725E58`) contain exact-value REF waits with `usleep(30)` retries.
Before changing their semantics, check whether reference values are being
queued and published during gameplay. `usleep(30)` currently uses a QPC/yield
loop on Windows; the existing 330-microsecond job-wait replacement does not
cover these sites. An event on the current REF word alone can miss intermediate
values and is not an equivalent completion contract.

`scripts/observe_ref_counters.py` verifies the exact owned executable, holds a
query/read-only process handle, records process creation time and executable
hash, then resolves scalar symbols from its adjacent PDB. It never suspends
threads or writes target memory. It reads publication count, frame count and
optional queue head/tail every 250 milliseconds. Each non-atomic snapshot has
a QPC bracket; setup precedes that observer's collection clock. Output is
buffered until the duration completes or the process exits. The ready file is
only setup evidence; the final status establishes completion.

The effective REF address is `VM_HLE_INJECT_BASE + 0x2008`, currently
`0x20002008`; a fixed `0x03002008` comment in the loader is stale. The counter
`g_gcm_ref_pub_count` increments only upon actual publication. Both the FIFO
drain and poll hook can publish, so it does not count polls, lock misses,
empty-queue checks or guest wait iterations. Queue positions are separate
reads and cannot establish instantaneous occupancy during concurrent updates.

## Live evidence

Muted run `20261014-003941-722`, PID 49884, uses the ordinary
`deferred-texture-logs` binary (code Haze `a11df11`, SDK `f6cba5c`), SHA256
`FAEAF863B41475F3206A5B3E1A7BA0B341F95797223E0BDC23704FA4B4A430CF`.
Settings: 2560x1440, 8192 shadows, FXAA, AF16, VSync off, no frame cap;
CPU 13571, GPU occurrences 2, VP burst 1, worker reuse enabled. Job-wait,
inline-read/coherency, bulk-fill, milestone and slow-renderer diagnostics are
off. Private copies of the real jungle save, graphics config and shader-cache
bundle are used. Diagnostic PAD_FILE input is acknowledged detached before
camera sweeps.

All three observers identify the same process creation time and binary. Every
sample has publication count, queue head and queue tail equal to **366**:

| Observation | Snapshots | Observed frame counters | First-to-last snapshot span |
| --- | ---: | --- | ---: |
| Navigation probe | 4 | 23102–23506 | 0.752 seconds |
| Jungle preparation | 719 | 46041–62073 | 179.792 seconds |
| Camera sweep through normal close | 142 | 67341–70625 | 35.316 seconds |

The first two collectors complete their requested durations; the last reports
`target_exited`, exit code zero, without read failures. Maximum polling gaps
are 250.595 / 252.584 / 257.853 milliseconds; maximum snapshot brackets are
36.4 / 359.1 / 986.1 microseconds. These brackets include observer scheduling,
not time spent suspending the game. No counter reset occurs in the production
path. Unchanged cumulative publication count and queue positions across the
observations argue against a queue of paced REF publications explaining steady
frame cost. They do not prove absence of REF reads or guest helper calls.

The second observer ends before the timed sweeps; the third starts just after
the second sweep begins. Its PDB setup overlaps that diagnostic window.
Do not claim continuously sampled coverage of both sweeps. The unchanged
cumulative values bracket the gap, and the recorded second-sweep hitch itself
falls within the third observer's span.

## Frame context and remaining lead

Two 30-second absolute camera sweeps (tokens 3640/3641) retain all 5,601 complete
present intervals: **93.387 FPS / 10.708 ms mean**, 13.461 ms p95, 15.554 ms p99,
51.333 ms maximum. There is one frame above both 33.333 and 50 milliseconds.
Individual windows average 94.120 / 92.654 FPS. Pump means are 5.556 ms command
wait, 3.282 ms FIFO, 1.809 ms present and 0.062 ms other.

The measured windows pass camera/mode/present and sampled Haze-process gates.
Fifteen resource samples bracket timing, maximum gap 5.359 seconds, physical
memory 52.20–52.89%, no other Haze or compiler observed. Four external compilers
were present before launch but none in the measured resource observations.
No owned build, screenshot or heavy analysis overlaps timing. The final
pre-window image still has the yellow Nectar effect, unlike the previous
settled-scene capture. Together with PDB setup and counter observation, that
prevents treating these numbers as a matched ordinary-build gain or acceptance
result. Earlier tutorial/setup captures and failed scene matching are retained.

Worst pump frame **68063** spans QPC `14508942170385..14508942683721` at 10 MHz:
51.3336 ms total, **44.3795 ms command wait**, 4.7138 ms FIFO, 2.1642 ms present.
There are 31 timeouts totaling 44.0920 ms, three signals totaling 0.2875 ms,
one pre-wait queued observation and three post-wait queued observations.
The longest individual wait is 9.8699 ms. The 1-ms timeout is a requested
deadline, not a scheduling guarantee; this does not identify a guest sleep
or establish that the whole wait scope is removable CPU time.

Next, sample the main command-producing thread during ordinary work and
correlate its execution/waits with pump intervals. Leave REF pacing and the
normal FIFO timeout unchanged. A previous zero-timeout experiment increased
empty drains dramatically without demonstrating a frame-pacing benefit.
Unlocked/VSync behavior still needs broader acceptance, and consistent pacing
at 144 FPS (6.944 ms/frame) remains unachieved.

The game exits normally, all seven immutable save hashes match, and raw evidence
is retained under `local/jungle-ref-activity-a1-*`. No game assets, saves or
generated code are included. The observer passed Python syntax checking,
read-only source review and these live identity/size/read/exit checks; no full
repository test-suite claim is made.
