# Native byte and halfword reads

Tracked under [#38](https://github.com/zdiemer/haze-recomp/issues/38).

The SDK already bypasses diagnostic bookkeeping for ordinary native 32-bit
and 64-bit guest reads. Its equivalent byte/halfword paths were restricted to
Emscripten. Lifting those two platform guards makes the existing fast paths
available to the native build, with no new user option. The bounds check still
runs first. Low addresses and enabled memory diagnostics retain the original
path. Halfword reads use memcpy and a byte swap, preserving unaligned access
and big-endian guest values. No store, reservation or device behavior changes.

`tests/test_memory_read_fastpaths.py` extracts the actual SDK accessors and
their bounds/probe guards. It also reconstructs the previous native behavior
by guarding the exact fast branches. Separate native executables/processes
test byte/halfword values, unaligned halfwords, final valid byte/word, OOB,
low addresses, forced byte values, each of the 15 diagnostic environment
variables independently, and a `VM_SAMPLE_READS` build. Unrelated diagnostic
functions are stubs: these tests establish value and route preservation, not
the implementation of every diagnostic subsystem. All 41 checks pass in the
standard `build.ps1 -SpuFma -Test` gate, along with the full regression suite.

Candidate `local/candidates/native-memory-reads/HazeRecomp.exe` has SHA256
`381A1E86252AAF7F64DB1AF9514A4B8275137DEDC7022A4C536CB2E83065DC4B`.
Muted run `20261013-025051-707` used private input/saves, 1440p/follow shadows,
VSync on, no cap, GPU occurrence mode 2 and native index preparation. The
native VP upload experiment remained off. Capture `requested_013089.ppm`
confirms the carrier; normal close returned exit 0. This is a gameplay smoke
check, not a paired performance comparison or full-campaign validation.

A separate 16.003-second diagnostic sampled four active threads, selected by
a preceding three-second thread CPU scan. It used 1,422 rounds, stack walks
every fourth round, and a 9 ms randomized sampling interval. Its 5,688 brief
suspensions total 1,418.29 ms (maximum 4.66 ms). Sampling perturbs execution;
these intervals must not be used as FPS benchmarks.

| Thread | Work | CPU ms |
| --- | --- | ---: |
| 54880 | Main guest thread 1 | 11781.250 |
| 55756 | Guest thread 8, timer/event polling | 9312.500 |
| 64272 | Renderer/FIFO consumer | 6265.625 |
| 65876 | SPU image 26 and audio | 5000.000 |

Process CPU is 78312.5 ms, of which 32359.375 ms belongs to these four selected
threads. The remainder includes other jobs and is not attributed to one
subsystem. Main-thread samples still include memory helpers and genuine job
waits. Renderer samples include `rsx_process_method`, `rsx_dispatch_method`,
FIFO parsing and hashing, which support investigating consumer batch dispatch.
RIP/stack sample counts include waits and are not CPU utilization percentages.
No whole-frame speedup is established for the byte/halfword change yet.

The matching candidate disassembly identifies sampled `vm_read8+0x1EE` as
the ordinary VM byte load followed by return, confirming runtime use of the
new path. Renderer PDB/disassembly mapping also identifies
`rsx_process_method+0x6B8/+0x6CD/+0x6E8/+0x6F5` as VP instruction-word
upload bounds/stores/cursor updates, and `rsx_dispatch_method+0xD3` as its
VP instruction upload path. Other samples hit texture dispatch, transform
constants and generic bookkeeping. These are evidence of executed paths,
not a measured CPU share or promised gain from batching them.

Private evidence: `local/native-memory-reads-build-tests.log`,
`local/native-memory-reads-thread-scan.json`,
`local/native-memory-reads-cpu-sample.json` (UTF-16), and the run above.
