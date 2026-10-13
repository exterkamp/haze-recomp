# Real-checkpoint loading and native job boundaries

Follow-up for #9, #12, #37 and #39. Neither these diagnostic results nor the
static analysis establish an ordinary-build FPS gain.

## Real checkpoint resume

The user's real save was copied from `local/state/savedata` into an immutable
private baseline, with two matching reads of every file and verified copy
hashes. A disposable copy was used for run `20261013-051211-596`, PID 16800.
The original save was not modified. Candidate is the deferred-profiler build
from [the recorder report](performance-deferred-profile-2026-10-13.md), SHA256
`376B0C1C5672F74E59B196AE985120394072A7CFEF17B77DA5B7E643787CA49F`.

Settings follow the interactive WIP: 2560x1440 rendering, native shadows,
borderless output, VSync on, cap zero, GPU occurrence mode 2, CPU flags 13571,
native VP consumer on. Automated audio remains muted with real processing.
The existing shader disk cache was retained; this was not a cleared-cache test.

Menu captures verify Campaign -> Single Player -> Continue Campaign and its
confirmation. Confirm input is timestamped QPC `13805747204007`, frequency
10,000,000. No screenshots occur during the next 45 seconds. The resulting
scene is the jungle Nectar tutorial; the user has been asked whether this is
the intended busy-area checkpoint. No settled combat FPS is claimed. Subsequent
captures and inputs advance the tutorial, but do not complete that benchmark.

| Frame | Seconds after confirm | Interval ms | PSO preparation ms | Pipeline creation ms | Shader disk-load scope ms |
|---|---:|---:|---:|---:|---:|
| 32597 | 11.989 | 597.878 | 484.742 | 471.299 | 7.158 |
| 32602 | 12.733 | 304.967 | 290.700 | 285.233 | 2.289 |
| 32738 | 16.424 | 781.483 | 748.547 | 3.199 | 741.023 |
| 33077 | 23.088 | 627.525 | 613.676 | 1.176 | 611.204 |

Shader compilation is zero in these rows. The two large disk-load scopes
contain 29 and 2 calls respectively, with only 0.047/0.006 ms in blob creation.
All disk-load scopes within the first 45 seconds total 1,441.183 ms. These are
elapsed scopes including file operations, allocation, validation, logging and
scheduling; the findings do not prove the storage device spent that time
reading. The first large scope also coincides with the cache's 128-pixel-shader
hit progress message. The other does not have a corresponding progress message
between adjacent 32-frame live-draw reports. Split open/read/close/report timing
before choosing a preload or logging fix.

Normal close saves 32,768 recent renderer/geometry rows out of 50,127, with no
rejected records. They retain the complete first-45-second period and earlier
rows. The smaller pump ring overwrote the initial load and starts 32.171 seconds
after confirmation: do not invent a disjoint pump partition for those first
stalls. The process observer records only the owned Haze during initial resume;
other host work is not excluded. Ghidra's full import had finished; short
read-only follow-up analysis also occurred during this broader session.

Private evidence: `local/real-resume-baseline.json`,
`local/real-resume-profile-load-marker.json`, `local/real-resume-profile-*`,
the run's logs and explicitly requested captures. Next capture should navigate
menus promptly and dump before ring retention loses the initial phase.

## Ghidra job audit

Official Ghidra 12.1.4 with portable Java 21 successfully imports the decrypted
EBOOT as `PowerPC:BE:64:64-32addr`, default compiler model. The private project
uses a 4-GiB heap and two-CPU analysis limit. Follow-ups reuse it with
`-noanalysis -readOnly`; no game code or decompiled output is committed.

`00682648` is the concrete submission boundary for the title's 512-slot table.
It marks a slot pending, copies a 0x1410-byte payload, stores mode and metadata,
returns the slot ID through an output pointer, and records completion descriptor
and argument at offsets +0x1430/+0x1434. This advances the earlier wait-only
audit and provides a place to correlate generation, payload and handler with
the measured waits.

Submission `007E7494`, paired with wait family `007E5414`, requests mode 3 with
lookup key zero. In the ELF's initial table, that resolves to mode zero and
descriptor `00E6DDD8`, code `00682900`, TOC `00E94EE0`. That handler calls
`007E56E0` with four payload fields. **The table is writable; this is a static
default, not a verified live dispatch.** The completion descriptor address
`012B8660` lies in an uninitialized writable section and needs runtime tracing.

`007E56E0` blends four indexed 0x40-byte matrices using four weights per
0x30-byte input record, then performs further transform conversion and writes
0x1C-byte output records. This identifies an algorithm suitable for a potential
whole-function native kernel; no symbol proves the job's gameplay name. Measure
live invocation, count, resolved mode/handler and elapsed cost before selecting
it as an optimization target.

The other hot waits occur during list/record recycling. Result-queue draining
inside the completion predicate remains required. A future trace must correlate
submission and completion by slot generation, not merely reused slot index, and
record resolved dispatch separately from the requested mode.

## Texture resource reuse candidate

A separate source audit identifies a potential in-place 2D texture refresh
boundary. Changed cached content currently allocates a new resource and retires
the old one. Matching full resource descriptions and view semantics could
permit ordered transitions/copies into the existing texture on the same queue.
First measure miss-versus-refresh allocation and post-fence release costs.
Fallback is required for incompatible formats, override changes, cubemaps,
upload-capacity failures and unaudited states. This candidate is not implemented
and its potential frame-time saving is unknown.
