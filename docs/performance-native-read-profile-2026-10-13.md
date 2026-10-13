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
read checks also pass. Full-game build and ordinary FPS comparison results
remain pending; these checks establish no frame-time gain.
