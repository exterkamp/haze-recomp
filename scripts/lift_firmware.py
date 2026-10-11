#!/usr/bin/env python3
"""Translate local ATRAC firmware at Haze's observed SPU decoder allocation.

No firmware bytes or generated translations are distributed by this repository.
ELF relocation changes metadata only: the decoder itself is position independent.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SDK = ROOT / "external/ps3recomp"
GENERATED = ROOT / "generated"


def relocate_elf(original: bytes, delta: int) -> bytes:
    if original[:6] != b"\x7fELF\x01\x02" or len(original) < 52:
        raise ValueError("Expected a big-endian ELF32 firmware image")
    data = bytearray(original)
    u16 = lambda p: struct.unpack_from(">H", data, p)[0]
    u32 = lambda p: struct.unpack_from(">I", data, p)[0]
    def shift(p: int) -> None:
        result = u32(p) + delta
        if not 0 <= result < 0x40000:
            raise ValueError("Relocated firmware exceeds SPU local store")
        struct.pack_into(">I", data, p, result)
    if u16(18) != 23:
        raise ValueError("Firmware is not an SPU ELF")
    phoff, shoff = u32(28), u32(32)
    phsize, phnum, shsize, shnum = u16(42), u16(44), u16(46), u16(48)
    if phsize < 32 or shsize < 40 or phoff+phsize*phnum > len(data) or shoff+shsize*shnum > len(data):
        raise ValueError("Truncated ELF metadata")
    shift(24)
    for i in range(phnum):
        ph = phoff+i*phsize
        if u32(ph) == 1:
            shift(ph+8)
            shift(ph+12)
            if u32(ph+8)+u32(ph+20) > 0x40000 or u32(ph+4)+u32(ph+16) > len(data):
                raise ValueError("Invalid load segment extent")
    allocated = set()
    for i in range(shnum):
        sh = shoff+i*shsize
        if u32(sh+8) & 2:
            allocated.add(i)
            shift(sh+12)
    for i in range(shnum):
        sh = shoff+i*shsize
        if u32(sh+4) not in (2, 11):
            continue
        offset, size, entry_size = u32(sh+16), u32(sh+20), u32(sh+36)
        if entry_size < 16 or size % entry_size or offset+size > len(data):
            raise ValueError("Invalid symbol table extent")
        for entry in range(offset, offset+size, entry_size):
            if u16(entry+14) in allocated:
                shift(entry+4)
    return bytes(data)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dev-flash", type=Path, default=Path(os.getenv("PS3_DEV_FLASH", ROOT / "local/tools/rpcs3/dev_flash")))
    args = parser.parse_args()
    source = args.dev_flash / "sys/external/flashATRAC.pic"
    original = source.read_bytes()
    relocate_elf(original, 0)  # Validate metadata before unpacking below.
    # BLUS30094's Multistream worker reserves decoder storage ending at 0x3B000.
    # Verified with a native LS capture: this image's code starts at 0x33180.
    phoff = struct.unpack_from(">I", original, 28)[0]
    phsize, phnum = struct.unpack_from(">HH", original, 42)
    segments = [struct.unpack_from(">8I", original, phoff+i*phsize) for i in range(phnum)]
    loads = [p for p in segments if p[0] == 1]
    end = max(p[2]+p[5] for p in loads)
    base = 0x3B000 - ((end+127) & ~127)
    # Other firmware allocations have not yet been validated against Haze.
    if base != 0x33100:
        raise ValueError(f"Unvalidated Haze decoder allocation {base:#x}; expected 0x33100")
    relocated = relocate_elf(original, base)
    stage = GENERATED / "firmware"
    stage.mkdir(parents=True, exist_ok=True)
    elf = stage / "flashATRAC.relocated.elf"
    elf.write_bytes(relocated)
    output = GENERATED / "spu/firmware_atrac"
    lift_relocated(original, base, stage / "flashATRAC", output, "firmware_atrac_")
    sys.path.insert(0, str(SDK / "tools"))
    from build_spu_workloads import debase_includes, fnv1a64
    for path in output.glob("spu_recomp.*"):
        debase_includes(str(path))
    code = next(p for p in loads if p[6] & 1)
    shoff = struct.unpack_from(">I", original, 32)[0]
    shsize, shnum = struct.unpack_from(">HH", original, 46)
    sections = [struct.unpack_from(">10I", original, shoff+i*shsize) for i in range(shnum)]
    code_end = max(s[3]+s[5] for s in sections if s[2] & 4)
    fingerprint = fnv1a64(original[code[1]:code[1]+code[4]])
    registry = GENERATED / "spu/firmware_register.c"
    registry.write_text(f'''/* Generated from locally supplied firmware. */
#include "spu_context.h"
#include "spu_overlay_registry.h"
extern void spu_begin_image(int);
extern void firmware_atrac_spu_recomp_register(void);
__attribute__((constructor)) static void haze_atrac_register(void) {{
    spu_begin_image(46);
    firmware_atrac_spu_recomp_register();
    spu_overlay_register_fingerprint_at(0x{fingerprint:016X}ULL, {code[4]}, {code_end-code[2]}, 0x{base+code[2]:X}, 46);
    spu_begin_image(0);
}}
''', encoding="utf-8", newline="\n")
    (stage / "provenance.json").write_text(json.dumps({
        "source": str(source.resolve()), "sha256": hashlib.sha256(original).hexdigest(),
        "relocation_base": hex(base), "image_id": 46,
        "code_lsa": hex(base+code[2]), "code_span": code_end-code[2],
    }, indent=2)+"\n", encoding="utf-8")
    print(f"Translated local ATRAC decoder at LS {base+code[2]:#x}; source SHA256 {hashlib.sha256(original).hexdigest()}")


def lift_relocated(original: bytes, delta: int, stage: Path, output: Path, prefix: str) -> None:
    """Discover PIC entries before relocation, preserving address-valued seeds.

    ILA constants stay relative to the original link address in PIC code. The
    guest adds the actual relocation at runtime; changing ELF metadata before
    discovery hides these indirect entry points from the detector.
    """
    sys.path.insert(0, str(SDK / "tools"))
    from find_spu_functions import detect_functions
    from build_spu_workloads import debase_includes
    bounds, (offset, base, size) = detect_functions(original)
    shoff = struct.unpack_from(">I", original, 32)[0]
    shsize, shnum = struct.unpack_from(">HH", original, 46)
    sections = [struct.unpack_from(">10I", original, shoff+i*shsize) for i in range(shnum)]
    code_end = max(s[3]+s[5] for s in sections if s[2] & 4)
    bounds = [(start, min(end, code_end)) for start, end in bounds if start < code_end]
    code = stage.with_suffix(".code.bin")
    functions = stage.with_suffix(".functions.json")
    code.write_bytes(original[offset:offset+size])
    functions.write_text(json.dumps([{"start": start+delta, "end": end+delta}
                                     for start, end in bounds]), encoding="utf-8")
    subprocess.run([sys.executable, str(SDK / "tools/spu_lifter.py"), str(code),
                    "--base", hex(base+delta), "--functions", str(functions),
                    "--symbol-prefix", prefix, "-o", str(output)], check=True)
    for path in output.glob("spu_recomp.*"):
        debase_includes(str(path))


if __name__ == "__main__":
    main()
