# Job wait phases by pump frame — 2026-10-13

I joined the retained job trace to pump intervals using `scripts/analyze_job_wait_frames.py`. The capture covers pump frames 55940-56498 (559 intervals) in QPC window 13922887926488-13922976476565 at 10 MHz. All 6,078 wait spans overlapping the window matched a title slot generation, SDK job, and state-2 publication; none were skipped. Every attributed phase tick landed in a complete pump interval, and the trace retention cutoff precedes the window. The related source and native job-boundary context is in [the async job-boundary report](performance-resume-and-job-boundaries-2026-10-13.md). This capture predates `37ffeb7` and does not measure current frame rate.

| Group | Pump dt | Pump wait | FIFO | Present | Matched wait union | Worker startup | ABI execution | Pump interval outside matched waits |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Fastest half (280 frames) | 14.09 ms | 8.43 ms | 3.76 ms | 1.83 ms | 5.68 ms | 0.46 ms | 3.16 ms | 8.41 ms |
| Slowest 5% (28 frames) | 24.77 ms | 17.01 ms | 5.22 ms | 2.22 ms | 12.12 ms | 4.64 ms | 4.37 ms | 12.65 ms |
| Slowest 1% (6 frames) | 40.22 ms | 31.48 ms | 5.38 ms | 2.04 ms | 26.86 ms | 18.34 ms | 4.63 ms | 13.36 ms |

The strongest tail correlate is the interval from the instrumented SDK submit point until worker setup begins. Its mean is 18.34 ms in the slowest 1%, against 0.46 ms in the fastest half. ABI execution rises more modestly, from 3.16 to 4.63 ms. The slowest individual pump frame is 56418 at 49.24 ms; its worker-startup interval is 21.54 ms and ABI intervals total 6.45 ms across images 30, 41, and 43. Frame 56243 is 48.08 ms, with 25.25 ms startup and 6.71 ms ABI time. These results support measuring a worker-reuse A/B against startup tail latency; they do not prove all startup time is avoidable or establish the behavior of the new path.

For the slowest 1%, mean ABI wall time by image is 1.75 ms for image 30, 1.87 ms for image 41, 0.82 ms for image 43, and 0.20 ms for image 32. The wait phase intervals came from one pump host thread (16868), so phase partitions do not overlap in this capture. Pump CPU samples were reported valid for all group rows, but the mean is quantized (5.21 ms in the slowest 1%; individual values include 0.0 and 15.625 ms). Treat it as coarse thread-accounting context, not a per-phase CPU attribution.

The pump interval outside matched waits averages 13.36 ms in the slowest 1%. It includes time not represented by these observed, joined title waits; it is not an estimate of CPU work or removable cost. FIFO and present are pump-thread wall scopes and are already separate from the wait scope. A wall wait interval identifies what the pump thread was waiting through, not what another worker consumed on CPU. The next comparison should retain the same capture fields and compare the startup distribution and slow-frame percentiles, while checking FIFO, present, and the unjoined interval for regressions.

The retained trace contains 262,144 consecutive event tickets 8,866,729-9,128,872. Its summary reports zero missing tickets, busy/stale/frozen drops, incomplete scopes, and missing parents. The last discarded end-QPC (13,922,887,797,936) precedes the measured window start (13,922,887,926,488). The analyzer checks these ring counters and tickets, plus constant QPC frequency, contiguous pump intervals, consecutive frame IDs, `dt_ms` against QPC duration, and matching pump/trace/window clocks. It rejects gaps, drops, incomplete scopes, missing parents, or a discarded interval extending into the measured window.

CPU contention is not excluded. The pressure observer had expired before this block. Two other `clang` processes appeared in its last sample, but their activity during the capture is unknown. Pump-thread Windows CPU accounting is coarse and quantized; trace phases measure wall intervals rather than phase CPU. Startup-to-setup wall time may reflect scheduling, dispatch, or worker availability. This capture cannot separate those causes or show which portion worker reuse can remove.

The generated detailed result is `local/jungle-job-async-wait-frames.json`. Reproduce it with:

```powershell
.venv\Scripts\python.exe scripts\analyze_job_wait_frames.py `
  local\jungle-job-async-trace.csv `
  --windows local\jungle-job-async-retained-windows.json `
  --pump local\jungle-job-async-pump.csv `
  --output local\jungle-job-async-wait-frames.json
```

Synthetic conservation, clipping, reused-slot, concurrent-host, retained-ring, and kind-15 overlap/failure checks pass with:

```powershell
.venv\Scripts\python.exe -m unittest tests.test_job_wait_frames tests.test_job_trace_analysis
```
