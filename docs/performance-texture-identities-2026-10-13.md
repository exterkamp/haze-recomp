# Guest-address churn in the jungle texture cache

The larger-cache experiment did not eliminate allocations. A new bounded
miss trace in SDK `76b10de` identifies what differs between those cache keys.
The trace stores descriptors, already-computed content hashes, cache slots,
use serials and eviction state. It makes no additional guest-memory reads and
does no file I/O while recording. It retains at most 65,536 rows in about
20 MiB, dumps on normal shutdown, and exists only in profiling builds.

## Capture and validation

Run `20261013-063936-228`, candidate `texture-identities`, uses a fresh copy of
the confirmed private jungle save. Screenshot verification establishes the
Nectar tutorial and its dismissal before two 24-second, 30-degree camera
sweeps. Settings remain 1440p, native shadows, VSync on, uncapped, muted real
audio, native vertex-program consumer and GPU occurrence decoding enabled.
The texture cache is back at 4,096 entries. Candidate SHA-256:
`DF63277B78CD4EE7A4C475CCD50593BDE958F7D710C958A04A89CEF1CE8B1AE7`.

All 9,226 miss records are retained, with consecutive event IDs, monotonic
QPC/use serials and successful decodes. All 22,142 renderer rows pass existing
119-column phase/count checks. Both recorders report zero rejected rows.

`scripts/analyze_texture_misses.py` reconciles every measured renderer row
against the trace. Misses recorded with guest frame N contribute to renderer
frame N+1 because ordinary presentation increments the frame before profiling.
Event-QPC window selection alone differs legitimately at boundaries: the first
window contains 2,310 events by event time, but 2,311 by matching the selected
renderer frames. The second has 2,190 by either method. Both per-frame audits
pass. The analyzer tests cover this distinction, truncation, malformed values,
continuity, bounded retention, mismatched counts and content-group semantics.

## Finding: address identity dominates

Across the complete capture there are 9,192 distinct descriptors but only 369
groups when guest location/offset are excluded and the existing content hash
is included. All other descriptor fields are retained in that grouping:
format, dimensions, pitch, remap, cubemap flag, mip count and dimension type.
Of 9,226 misses, 9,044 belong to groups observed at multiple guest addresses.
None has a zero hash. Only 34 exact descriptors miss twice.

Ignoring pitch merges no descriptors in this capture; ignoring mip count
merges just one. Therefore the plausible pitch-normalization optimization
does not address this observed churn. Address changes are the stronger lead.

| Event-time window metric | Sweep 1 | Sweep 2 |
| --- | ---: | ---: |
| Miss events | 2,310 | 2,190 |
| Unique full descriptors | 2,310 | 2,190 |
| Layout plus content-hash groups | 99 | 93 |
| Groups seen at multiple addresses | 92 | 91 |
| Events in those groups | 2,303 | 2,188 |

Every measured miss is BC1 or BC3. For example, one 128x128 BC1 image with
eight mips appears at 62 addresses in the second window with the same layout
and hash. This strongly suggests repeated relocation of texture contents.
Hashes alone do not prove exact byte equality or authorize merging resources.

## Performance limits

Other Haze instances overlap both windows; the strict process-isolation audit
fails and is preserved. Separate diagnostic reports record 41.05/45.68 FPS,
mean intervals 24.36/21.89 ms, p95 39.67/33.56 ms, p99 62.40/51.51 ms and maxima
346.79/155.14 ms. These are not accepted isolated baselines, and cannot measure
the instrumentation's overhead or a performance regression. No optimization
was enabled in this build. Allocation averages 1.125/0.996 ms per renderer row
within these non-isolated windows; prior isolated recovery averaged about
0.88 ms. No compiler or screenshot from this agent runs during the windows.

## Native replacement justified by this evidence

Issue #59 should prioritize sharing immutable native texture storage across
guest addresses, while retaining per-descriptor view and mutation tracking.
A bounded cache of native upload content can compare format, mip layout and
every source byte against an immutable snapshot before returning a shared GPU
resource. Hashes may accelerate lookup but must not be the equality test.
Host allocation and snapshot bytes both need budgets. A changed source must
get the matching new resource rather than mutate a resource another descriptor
still references. Existing retirement fences and explicit COM ownership must
keep earlier draws safe. Overrides and unsupported paths retain their existing
behavior; shared resources must preserve independent remapped views.

This replacement should be a separate commit with GPU readback tests for
relocated identical data, changed data, differing layouts/views, forced hash
collisions, eviction/lifetime and upload failure. Follow it with the same
checkpoint capture, reporting actual allocation/upload savings and frame
tails. The trace establishes a concrete reuse opportunity, not a measured
speedup or completion of the 144-FPS goal. The delivered WIP is unchanged.

Private evidence uses `local/jungle-texture-identities-*` and
`local/texture-identities-build.log`. Assets, saves, traces and binaries are
not committed.
