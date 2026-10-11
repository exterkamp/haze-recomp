"""Repeatable Haze binary inventory and ps3recomp analysis/lifting commands."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SDK = ROOT / "external" / "ps3recomp"
DISC = ROOT / "game" / "disc"
ELF = DISC / "PS3_GAME" / "USRDIR" / "EBOOT.ELF"
ANALYSIS = ROOT / "analysis"
GENERATED = ROOT / "generated"


def write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def sfo_metadata(path: Path) -> dict:
    data = path.read_bytes()
    magic, version, keys, values, count = struct.unpack_from("<5I", data)
    if magic != 0x46535000:
        raise ValueError("PARAM.SFO is unreadable; check disc decryption")
    result = {}
    for i in range(count):
        key, kind, size, maximum, offset = struct.unpack_from("<HHIII", data, 20 + 16 * i)
        name = data[keys + key:].split(b"\0", 1)[0].decode("utf-8")
        value = data[values + offset:values + offset + size]
        result[name] = (struct.unpack("<I", value)[0] if kind == 0x0404
                        else value.rstrip(b"\0").decode("utf-8", errors="replace"))
    return result


def run(tool: str, *args: object, report: Path | None = None) -> None:
    cmd = [sys.executable, str(SDK / "tools" / tool), *(str(a) for a in args)]
    print("Running", tool, flush=True)
    if report:
        report.parent.mkdir(parents=True, exist_ok=True)
        with report.open("w", encoding="utf-8") as stream:
            subprocess.run(cmd, cwd=ROOT, stdout=stream, check=True)
    else:
        subprocess.run(cmd, cwd=ROOT, check=True)


def inventory() -> None:
    eboot = DISC / "PS3_GAME" / "USRDIR" / "EBOOT.BIN"
    data = eboot.read_bytes()
    revision = subprocess.check_output(["git", "-C", str(SDK), "rev-parse", "HEAD"], text=True).strip()
    result = {
        "metadata": sfo_metadata(DISC / "PS3_GAME" / "PARAM.SFO"),
        "ps3recomp_commit": revision,
        "eboot_bytes": len(data),
        "eboot_sha256": hashlib.sha256(data).hexdigest(),
        "eboot_format": "SELF" if data[:4] == b"SCE\0" else "ELF" if data[:4] == b"\x7fELF" else "unknown",
        "self_key_revision": hex(struct.unpack_from(">H", data, 8)[0]) if data[:4] == b"SCE\0" else None,
        "decrypted_elf_present": ELF.is_file(),
    }
    write_json(ANALYSIS / "inventory.json", result)
    print(json.dumps(result, indent=2))


def require_elf() -> None:
    if not ELF.is_file() or ELF.read_bytes()[:4] != b"\x7fELF":
        raise ValueError(f"A decrypted ELF is required at {ELF}; run scripts/decrypt.ps1 first")


def analyze() -> None:
    require_elf()
    run("elf_parser.py", ELF, "--all", report=ANALYSIS / "elf.json")
    run("ppu_loader.py", ELF, "--output", ANALYSIS)
    run("extract_spu_images.py", ELF, "--output", GENERATED / "spu-images")
    sys.path.insert(0, str(SDK / "tools"))
    from gen_hle_nids import discover_all, scan
    from nid_database import compute_nid
    # This reports symbol availability, not correctness of ABI bridges or HLE.
    implemented = {}
    for module in discover_all():
        for source in (SDK / "libs").rglob(module + ".c"):
            implemented.update(scan(str(source)))
    for filename in ("ppu_sysprx.cpp", "ppu_fs.cpp"):
        text = (SDK / "runtime" / "ppu" / filename).read_text(encoding="utf-8")
        for nid, name in re.findall(r'ps3_hle_register_ctx\(\s*(0x[\dA-Fa-f]+)u?\s*,\s*"([^"]+)"', text):
            implemented[int(nid, 16)] = name
        for name in re.findall(r'ps3_hle_register_ctx\(ps3_compute_nid\("([^"]+)"\)', text):
            implemented[compute_nid(name)] = name
    # Host-socket handlers are context registrations, outside gen_hle_nids's
    # exported-function scan, and require PS3_NET_ONLINE at startup.
    text = (SDK / "libs" / "network" / "sysNet.c").read_text(encoding="utf-8")
    for name in re.findall(r'\{\s*"([^"]+)"\s*,\s*h_\w+', text):
        implemented[compute_nid(name)] = name
    names = dict(implemented)
    # Optional official source references supply names, never implementations.
    for source in (ROOT / "local" / "reference" / "rpcs3").glob("*.cpp"):
        text = source.read_text(encoding="utf-8")
        for name in re.findall(r'REG_FUNC\(\s*\w+\s*,\s*(\w+)\s*\)', text):
            names[compute_nid(name)] = name
    imports = json.loads((ANALYSIS / "EBOOT.imports.json").read_text())
    coverage = []
    for item in imports:
        nid = int(item["nid"], 0)
        coverage.append({**item, "name": names.get(nid), "runtime_symbol_present": nid in implemented})
    write_json(ANALYSIS / "import-coverage.json", coverage)
    summary = {
        "imports": len(coverage),
        "runtime_symbols_present": sum(x["runtime_symbol_present"] for x in coverage),
        "missing": [x for x in coverage if not x["runtime_symbol_present"]],
        "network_imports": [x for x in coverage if any(t in x["library"].lower() for t in ("net", "np", "http", "ssl"))],
        "elf_sha256": hashlib.sha256(ELF.read_bytes()).hexdigest(),
        "note": "Symbol presence does not establish working HLE, rendering, gameplay, or multiplayer.",
    }
    write_json(ANALYSIS / "summary.json", summary)
    print(f"Imports: {summary['imports']}; runtime symbols present: {summary['runtime_symbols_present']}")


def lift(jobs: int) -> None:
    require_elf()
    if not (ANALYSIS / "EBOOT.functions.json").exists():
        analyze()
    # OPD-only is an initial lift. Heuristic/Ghidra recovery and SPU translation
    # may still be needed before the guest can boot or run correctly.
    run("ppu_lifter.py", ELF, "--functions", ANALYSIS / "EBOOT.functions.json",
        "--hle-stubs", ANALYSIS / "EBOOT.imports.json", "--jobs", jobs,
        "--output", GENERATED / "ppu")
    run("gen_hle_nids.py", "--all", "--out", GENERATED / "ppu" / "ppu_hle_nids.cpp")
    write_json(GENERATED / "provenance.json", {
        "elf_sha256": hashlib.sha256(ELF.read_bytes()).hexdigest(),
        "ps3recomp_commit": subprocess.check_output(["git", "-C", str(SDK), "rev-parse", "HEAD"], text=True).strip(),
        "function_source": "analysis/EBOOT.functions.json (OPD initial lift)",
    })


def lift_spu() -> None:
    require_elf()
    if not (GENERATED / "spu-images").is_dir():
        run("extract_spu_images.py", ELF, "--output", GENERATED / "spu-images")
    run("build_spu_workloads.py", "--images", GENERATED / "spu-images",
        "--lifted", GENERATED / "spu", "--out", GENERATED / "spu" / "spu_workloads.c",
        "--register-fn", "haze_spu_register_all", "--constructor", "--title", "haze",
        "--lift-overlays",
        # BLUS30094 Havok's small-island overlay streams mutable work data
        # starting at LS 0x17100. Keep its complete content fingerprint, but
        # limit compiled-code ownership to the preceding range.
        "--overlay-code-end", "spu_0022_at_00D15F80:3=0x17100")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("inventory", "analyze", "lift", "lift-spu"))
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    try:
        if args.action == "inventory":
            inventory()
        elif args.action == "analyze":
            analyze()
        elif args.action == "lift-spu":
            lift_spu()
        else:
            lift(args.jobs)
    except (ValueError, FileNotFoundError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"ERROR: {error}\n")


if __name__ == "__main__":
    main()

