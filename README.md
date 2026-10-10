# Haze Recomp

Experimental native Windows port of the PS3 version of **Haze**, using
[ps3recomp](https://github.com/sp00nznet/ps3recomp). This is a development
project, not a playable release.

## Target and requested features

The initial target is the USA disc, **BLUS30094**, disc version **01.02**,
application version **01.00**. Other regions and updates are not validated.

| Feature | Status |
| --- | --- |
| Standalone native Windows executable | Source generation and build bring-up in progress |
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
# After checking out the pinned SDK and supplying the game files:
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
  overlays. Upstream initially selected a 16-byte segment at `0x3000` and
  missed the actual entry at `0x200f0`. Overlay execution needs further work.
- The upstream runtime compiled locally with Clang 20.1.8.

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
