# Native CPU stack attribution: renderer and producer

This is CPU stack-attribution evidence, not an FPS comparison. The capture
used root `5f4b64c`, SDK `03571e1`, executable SHA-256
`35BB810181F25B9CA5881BC0227BE4DC6E5E4683DBEC16C31F1BC4985A6E969A`, and
the visually confirmed jungle checkpoint. Rendering was 2560x1440 with
8192 shadows, VSync off and no cap; GPU occurrence mode 2, CPU flags 13571 and
VP consumer 1 remained fixed. Native VP producer and native job wait were off.
Audio output was muted while mixing remained active. Two 45-second absolute
camera windows (tokens 3060/3061, five-second warmups) sampled the renderer and
producer separately. Their QPC frequency is 10 MHz:

| Sampled thread | QPC interval | Samples | Selected host TID |
|---|---|---:|---:|
| Renderer/window thread | `14291961968219..14292411990254` | 3,723 | 48096 |
| Producer thread | `14292468317282..14292918387286` | 3,590 | 7200 |

Both sample timelines retain every sample, with no ring overwrites. The
renderer is identified by the owned game window (`ps3recomp_null`); the
producer TID was selected explicitly. Each thread lifetime remained stable
through its 45-second capture. The sampler suspends threads to read their
contexts and unwind stacks, so these intervals are deliberately diagnostic,
not timing benchmarks. The profiler summary reports 776.309 ms of suspension
for the renderer and 2,101.593 ms for the producer, with maxima of 14.229 and
4.409 ms. The wider timeline brackets from before suspension through resume
sum to 816.761 and 2,147.560 ms because they also cover context and unwind
work. RIP counts below are sample counts, not CPU percentages, exclusive time
or estimated savings. Unwound stacks are available only on the configured
every-third sample.

The renderer's most frequent sampled RIP is `NtWaitForSingleObject` (2,353).
Among unwound samples, 629 show
`NtWaitForSingleObject -> WaitForSingleObjectEx -> frame_clock`; another 145
show the wait under `ld_flush -> rsx_live_draw_present`. Other renderer RIP
counts include `NtGetWriteWatch` 92, `fnv1a` 97, `gcm_rsx_process_fifo_unlocked`
55, `rsx_process_method` 38, `fp_hash_bytes` 38,
`texture_content_hash_kind` 33, `sink_end_impl` 27, and
`rsx_vp_analyze_inputs` 22. Thirty-two unwinds place
`NtGetWriteWatch` in `rsx_texture_write_watch_stamp -> texture_watch_stamp ->
texture_srv_slot`. These observations locate active waits, texture-watch
queries, hashing and draw/FIFO work; they do not measure each function's cost.

The producer's most frequent sampled RIPs include `NtDelayExecution` (952),
`vm_read32` (524), `vm_write64` (276), `vm_write32` (213),
`ZwWaitForAlertByThreadId` (144), `RtlQueryPerformanceCounter` (96),
`spu_coh_is_reserved` (95), `vm_read64` (59), `vm_read8` (49), and
`vm_write8` (29). Of the unwound samples, 307 show
`NtDelayExecution -> RtlDelayExecution -> SwitchToThread -> sys_timer_usleep`;
45 show `ZwWaitForAlertByThreadId -> RtlWaitOnAddress -> WaitOnAddress ->
gcm_fifo_wait_for_publication -> gcm_fifo_wait_for_ea`. Guest-memory helper
callers are spread across multiple functions. `func_0072C368` appears at the
sampled RIP 19 times, including read/write helper call sites; this is a lead,
not a claim that this function accounts for the aggregate helper counts.

The profile summary reports 15,500 ms of selected-thread CPU time for the
renderer and 38,468.75 ms for the producer over their respective capture
intervals. These counters and the sampled stack locations are complementary
observations; neither apportions the process's CPU use across renderer,
producer, waits or short-lived worker threads. The capture cannot establish
that a sampled wait is unnecessary or that a helper is a pacing bottleneck.

## Host observation limits

The process observer could not reuse its output path and requested a new path;
the retry is recorded in `local/jungle-synced-active-cpu-retry.log`. The
existing `processes.jsonl` is from the earlier attempt (its QPC values precede
these windows) and is stale. It was not used to claim process isolation. The
window manifest accordingly has no observed PID list. During the actual CPU
windows, the separate resource log records both the owned Haze PID 49328 and
another Haze PID 57812. Thus this capture does not demonstrate an isolated
host workload.

Resource intervals bracket both windows but are sparse: the largest gap
between one interval's end and the next interval's start is 5.041 seconds.
Nine intervals overlap the renderer window. The nearest preceding interval
ends 4.459 seconds before its start; the first following interval begins 3.518
seconds after its end. Eight intervals overlap the producer window; the
corresponding edge gaps are 1.793 seconds before and 0.814 seconds after. In
overlapping intervals, physical memory use ranges 48.33-49.04% for the
renderer window and 49.08-49.41% for the producer window; available memory is
32,176-32,624 MiB and 31,940-32,149 MiB, respectively. No compiler is listed
in any overlapping resource sample. Sampled page-input rates peak at 68/sec
and 60/sec. These sparse observations cannot exclude activity between
samples. The stale process log must not be joined to this capture to fill
those gaps.

Private capture files are `local/jungle-synced-active-cpu-render-stacks.json`,
`-render-timeline.json`, `-producer-stacks.json`, `-producer-timeline.json`,
`-windows.json` and `-pressure.jsonl`. The next planned experiment is a
generated inline fast path for guest read8/16/32/64 operations with the
existing checked fallback preserved. This profile nominates the helper family
for a controlled test; it does not establish a performance gain or justify
changing the fallback.

## Inline-read experiment and validation

SDK `c61b8bd` and Haze `21a429a` implement the experiment for #38. Only
generated PPU translation units remap read8/16/32/64. The startup-only
`PPU_INLINE_READS=ON` selector enables it when the existing memory diagnostics
are absent and VM storage exists. It defaults off pending ordinary frame
comparisons. There is no hot control-file polling or release settings entry.

Eligible reads truncate the guest address to 32 bits, require the complete
value to lie in `[0x10000, 0xE0000000)`, obey the current VM-size bound, and
load the original big-endian bytes directly. The read32 HLE ref-poll address
still calls the original accessor, including when the HLE region relocates.
All other unsupported reads retain the original call with its full input
address. Writes and their reservation/coherency behavior are unchanged.

An initial assembly fixture exposed a correctness hazard before gameplay
testing: plain inline loads let Clang remove a polling loop that repeatedly
read zero. The opaque legacy call had prevented that optimization. The
corrected implementation places compiler memory barriers before and after
each direct load. Compiled output retains the load inside the loop and emits
no hardware fence. Explicit guest fences retain their existing role.

Seventy fresh-process fixture cases pass, covering startup defaults and mode
selection, all 15 probe environment variables (present with both nonempty
and empty values), `VM_SAMPLE_READS`, null storage, address truncation,
unaligned big-endian values, end bounds, low/device/ref fallback, and unchanged
write routing. The enabled fixture also waits for a second thread to publish
a guest word. Assembly checks require the native load/byte swap, original
fallback call and retained polling loop. The existing 41 actual-accessor
read checks also pass. These checks establish no frame-time gain. The
subsequent ordinary two-job build and complete regression suite passed
(`local/inline-native-reads-build-tests-v2.log`).

## Ordinary whole-launch comparisons: no promotion

The candidate is `local/candidates/inline-native-reads/HazeRecomp.exe`, source
Haze `21a429a` / SDK `c61b8bd`, SHA-256
`5855EED98BA67C0EE0F3885A174E2946B6166E47F3AC4D7BE323F8EBA4EE7AD6`.
Each launch used a fresh copy of the seven-file private save baseline, the
settings above, visually confirmed jungle scenery after the Nectar prompt,
and two 60-second absolute camera sweeps with five-second warmups. Audio was
muted with mixing active. Diagnostic PAD_FILE polling was detached and
acknowledged before timing. No own compilation, CPU sampling or screenshots
ran in the timed windows. All frame intervals, including hitches, remain in
these numbers; all three runs exited normally with code 0.

| Mode / launch | Frames | FPS | Mean ms | p95 ms | p99 ms | Max ms | >33.333 / >50 ms |
|---|---:|---:|---:|---:|---:|---:|---:|
| ON `jungle-inline-on-b1` | 9,471 | 78.947 | 12.667 | 15.548 | 17.729 | 47.260 | 3 / 0 |
| OFF `jungle-inline-off-a1` | 9,039 | 75.345 | 13.272 | 16.708 | 19.393 | 65.348 | 2 / 1 |
| OFF repeat `jungle-inline-off-a2` | 10,013 | 83.458 | 11.982 | 15.118 | 17.505 | 73.772 | 2 / 1 |

ON passed the camera, actual-mode and process-isolation checks. Its resource
log observed no competing Haze or compiler, memory use 48.71–49.51%, with
26 bracketing samples and maximum 5.334-second sample spacing.

The first OFF run failed process isolation because Haze PID 48352 overlapped;
its result is retained but cannot establish a speedup. Its resource log
observed no compiler, memory use 52.10–53.42%, and maximum spacing 5.352s.

The repeated OFF run passed camera/mode/process checks and observed no other
Haze. Its resource log nevertheless caught two compiler processes near the
end of the second window (QPC 14318490851464..14318490984818; that window ends
14318572930827, 10 MHz). This prevents calling the entire launch an isolated
CPU comparison. Memory use was 50.65–51.48%, maximum spacing 5.333s. The first
60-second window had no competing game/compiler observed: 4,983 frames,
83.070 FPS, mean 12.038 ms, p95 15.358, p99 18.026, max 73.772. ON's first
window was 4,753 frames, 79.240 FPS, mean 12.620 ms. No time slices or hitches
were removed to improve either result. Sparse resource observations cannot
exclude activity between samples.

Mean pump phases (overlapping CPU/GPU work is not additive) were:

| Launch | Wait ms | FIFO ms | Present ms |
|---|---:|---:|---:|
| ON | 7.307 | 3.442 | 1.823 |
| OFF with Haze overlap | 7.654 | 3.624 | 1.878 |
| OFF repeat | 6.757 | 3.338 | 1.794 |

This implementation has not demonstrated a repeatable whole-frame gain.
The last earlier clean baseline was 94.045 FPS / 10.633 ms, but it used the
prior binary and a separate run; it is not a paired causal measurement of
the inline change. The experimental executable grew from 122,746,368 to
180,708,352 bytes. Extra code size and broad compiler clobbers are leads to
investigate, not measured explanations for the frame-time differences.

Normal builds therefore exclude the inline shim entirely, avoiding even its
disabled-path overhead. Developers can reproduce the experimental build with
`scripts/build.ps1 -SpuFma -Jobs 2 -ExperimentalInlineReads`, followed by a
fresh launch with `PPU_INLINE_READS=ON` or `OFF`. This is an experimental build
control, not a release graphics setting. Actual Ninja rules are checked to
ensure only the eight generated translation units receive the shim and that
ordinary builds receive none.

A follow-up candidate is a precise volatile unaligned RAM load, which may
preserve polling freshness without the full compiler clobber of unrelated
host context state. It requires assembly and concurrency checks before
building or timing; it is not implemented or promoted by these results.
