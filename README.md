# Haze Recomp

Experimental native Windows port of the PS3 version of **Haze**, using
[ps3recomp](https://github.com/sp00nznet/ps3recomp). This is a development
project, not a playable release.

## Target and requested features

The initial target is the USA disc, **BLUS30094**, disc version **01.02**,
application version **01.00**. Other regions and updates are not validated.

| Feature | Status |
| --- | --- |
| Standalone native Windows executable | Renders startup, title, main and campaign menus; campaign gameplay remains unvalidated |
| Campaign gameplay, rendering, audio, saves | Not validated |
| Online peer-to-peer campaign co-op | Not implemented or validated for Haze |
| Full keyboard and mouse controls | Planned; requires game input integration |
| PC key prompts | Planned; requires UI/asset work |
| Higher frame rates | Research after baseline gameplay works |
| Ultrawide | Research after baseline rendering works; projection and HUD must both be addressed |

The runtime building successfully does not establish that Haze works. Haze's
network imports include the older `sceNp` API and ticket requests; upstream
`sceNpMatching2` support alone does not supply Haze matchmaking. Direct peer
traffic may still need session discovery/signaling, NAT traversal, or a VPN.
The game's actual session protocol must be investigated before choosing one.

## Local development

Requirements: Windows x64, Visual Studio C++ tools and Windows SDK, Clang,
CMake, Ninja, Python 3.11+, Git, and 7-Zip. The build script discovers the
Visual Studio tools with `vswhere` and initializes their environment.

Game files, keys, downloaded helper tools, generated game translations and
build outputs stay local and are excluded from Git. Supply your own game files.
The source ISO is never modified. No PS3 game or firmware files are in this repo.

```powershell
git submodule update --init --recursive

# The supplied USA ISO is already readable at the disc level:
.\scripts\extract.ps1 -Iso 'S:\PS3\Haze (USA) (En,Fr,Es).iso'

# After supplying the game files:
python -m venv .venv
.venv\Scripts\python.exe -m pip install -r external/ps3recomp/requirements.txt

# Expected files: game/disc/PS3_GAME/PARAM.SFO and USRDIR/EBOOT.BIN.
# Decrypt the retail executable with a local official RPCS3 binary.
# RPCS3 is only a development helper; the target remains a native port.
.\scripts\decrypt.ps1 -Rpcs3 'C:\path\to\rpcs3.exe'

.venv\Scripts\python.exe scripts/haze.py inventory
.venv\Scripts\python.exe scripts/haze.py analyze
.venv\Scripts\python.exe scripts/haze.py lift --jobs 4

# Initial SPU translation; inspect warnings before accepting the output.
.venv\Scripts\python.exe external/ps3recomp/tools/build_spu_workloads.py `
  --images generated/spu-images --lifted generated/spu `
  --out generated/spu/spu_workloads.c `
  --register-fn haze_spu_register_all --constructor --title Haze

.\scripts\build.ps1 -RuntimeOnly
.\scripts\build.ps1

# Synthetic SDK regression checks (no game execution):
.\scripts\build.ps1 -RuntimeOnly -Test

# Bounded native boot diagnostic; capture later frames after initialization:
.\scripts\run.ps1 -Seconds 90 -CaptureFrames 4 -FrameSkip 3000 -FrameEvery 300
```

The scripts emit binary inventory, import coverage, loader information, and
translation provenance under ignored local directories. Import coverage is a
static inventory of symbols and context registrations; it is not an ABI or
behavioral compatibility test.

## Initial findings

- Retail SELF decrypted to a 16,039,384-byte ELF.
- Entry descriptor `0x00e35fb0`, entry code `0x00010230`, TOC `0x00e94ee0`.
- 44,520 OPD functions; the first PPU lift generated 58,637 functions.
- 216 firmware imports across 14 libraries.
- 44 embedded SPU ELF images; several produce unsupported `.word` warnings.
- SPU image at ELF offset `0x00d15f80` has multiple executable segments and
  overlays. The fork now selects the segment containing entry `0x200f0`,
  fixing the missing entry symbol. Seven synthetic regression cases pass.
  Overlay execution and unsupported instruction/data classification still
  need further work.
- The upstream runtime compiled locally with Clang 20.1.8.

## Verified local progress and current blockers

The Windows executable is `build/haze/HazeRecomp.exe`. Native boot probes
create PPU worker threads, execute lifted SPU jobs, read the game archives,
configure GCM display buffers, and submit D3D12 draw calls. A 120-second probe
completed roughly 1,700 SPU jobs and reached repeated pad polling and frame
submission. The live D3D12 renderer subsequently displayed Haze's splash/logo
and ESRB notice. After adding the deprecated SAIL lifecycle ABI, native tests
can skip the Ubisoft, Free Radical and attract movies with Cross and render
the Press Start title screen. The first-run installer now writes 72 files
(about 4 GB) to its local game-data USRDIR, and a subsequent boot passes the
installed-data check without reinstalling. Captures show the interactive main
and Campaign menus, including Single Player Campaign, Join Co-op and Host Co-op.
No campaign gameplay or co-op session has been validated;
a diagnostic timeout is not a successful gameplay test.

The SDK fork on its `haze` branch includes the executable-segment fix, Windows
DXGI GUID linkage, corrected game-data callback structure layout and first-run
directory status, and a fix that makes the unresolved-import error policy take
effect. Fixing the callback removed the corrupt-game-data dialog in a native
boot test. The installer also required 64-bit Windows stat/seek operations:
its 4,003,576,161-byte `install.qik` otherwise incorrectly appeared absent.
The fork creates the callback-approved game-data directories and persists
their PARAM.SFO metadata. Tests cover canceled checks, missing parameters,
metadata round trips, and actual stat/seek/open operations on an 8 GB sparse
fixture. Save-game persistence remains unvalidated.

Retail memory-container calls now route through syscalls 341/342, with 350
allocating from a real tracked container. The allocator reuses freed ranges
and protects mmapper reservations from overlap. This fixes invalid container
IDs and exhaustion after repeated startup buffers. Regression checks exercise
the syscall table, capacity and lifetime errors, 300 allocate/free cycles,
and reservation isolation. Campaign loading is still under investigation.

SPURS queue metadata now matches the lifted SPU consumer. PPU push/pop copy
payloads, update ring indices under the shared atomic lock, and signal SPU
waiters. A taskset scheduler scans for children created by persistent SPU
tasks, allowing Haze's controller to run its worker jobs. The DMA repeat
watchdog is disabled by default: repeated transfers are legal, and the former
default incorrectly halted a polling worker. Its optional diagnostic state is
now per SPU context. Synthetic tests cover these contracts, including queue
wrap/full/empty cases, task reuse and dispatch failure, and watchdog isolation.

The live native D3D12 renderer now displays the Haze splash/logo screen and
ESRB online notice. The legacy renderer displays the sky when its pass is
isolated, but the complete postprocessing chain goes black. The port defaults
to the live renderer; `RSX_LIVE_DRAW=0` or the diagnostic runner's
`-Renderer Legacy` selects the older path. A sample-projection heuristic no
longer overwrites Haze's valid negative matrix coefficient. Missing imports
observed during boot include
`cellSpursGetSpuThreadId` and `cellPadInfoSensorMode`. The deprecated
`cellSailPlayerInitialize` now has a separate ABI handler, including its ninth
stack argument. Player callbacks and adapter lifecycles let Haze use its own
movie skip path. Frame queries return explicitly empty frames; media decoding
is not implemented. Tests cover callback packing, object boundaries, slot
reuse, and empty-frame output. SPU overlays and unsupported instructions also
remain open.

Haze's executable contains Demonware lobby references and Ubisoft auth/STUN
hostnames. Peer-to-peer co-op will require tracing that game-specific session
path and replacing service dependencies where necessary. Adding a generic
socket transport by itself does not connect Haze's campaign state machines.

`scripts/run.ps1` returns errors for unregistered firmware imports and writes
logs, milestone counts, shader dumps, and optional frame captures to a unique
`local/runs/<UTC timestamp>` directory. `local/boot-result.json` points to the
latest run. `-FrameSkip` and `-FrameEvery` select later frames. The runner
terminates its process after the chosen timeout; `gameplay_validated` remains
false. The executable and runner default to `PS3_VERBOSE=0` because verbose
SDK logs cause thread contention and severely slow native execution; set it
to `1` for detailed diagnostics. Live-renderer captures use PPM; legacy
captures use BMP. Boot stack
reports are best-effort diagnostics. Queue operations no
longer require the upstream experimental environment flag.

## Development order

1. Resolve build, SPU segment selection, and executable boot failures.
2. Reach menus and correct single-player campaign gameplay with graphics,
   audio, timing, input, and saves.
3. Trace and implement Haze's session/authentication and peer traffic, then
   test a campaign session across two machines and different networks.
4. Add mouse/keyboard integration, prompts, frame-rate changes, and ultrawide
   projection/HUD fixes. Test gameplay timing and co-op after timing changes.

The SDK and port have separate histories. Commit and push source milestones;
never publish decrypted binaries or generated game translations.

The port scaffold derives from ps3recomp's MIT-licensed project template;
the original license and attribution are retained in [LICENSE](LICENSE).
