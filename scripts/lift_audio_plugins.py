#!/usr/bin/env python3
"""Translate BLUS30094's embedded PIC audio modules at their observed LS base.

The native Multistream worker loads these modules at 0x2F880 and 0x2F980. Generated code
and extracted game bytes remain local, like the primary PPU/SPU translations.
"""
from pathlib import Path
import hashlib
import json
import struct
import sys

from lift_firmware import GENERATED, SDK, relocate_elf, lift_relocated


def main() -> None:
    sys.path.insert(0, str(SDK / "tools"))
    from build_spu_workloads import fnv1a64
    stage = GENERATED / "audio-plugins"
    stage.mkdir(parents=True, exist_ok=True)
    declarations, registrations, provenance = [], [], []
    # Base 0x2F800 is the first plug-in allocation. The output plug-in (image 5)
    # is also observed at 0x2F900, following the resident 0x100-byte image 2.
    allocations = [(index, 0x2F800) for index in range(18)] + [(5, 0x2F900)]
    for ordinal, (index, delta) in enumerate(allocations):
        matches = list((GENERATED / "spu-images").glob(f"spu_{index:04d}_at_*.elf"))
        if len(matches) != 1:
            raise ValueError(f"Run scripts/haze.py analyze first; expected audio image {index}")
        source = matches[0]
        original = source.read_bytes()
        relocated = relocate_elf(original, delta)
        name = f"{source.stem}_base_{delta:05x}"
        elf = stage / (name + ".elf")
        elf.write_bytes(relocated)
        prefix = f"audio_pic_{index:02d}_" if delta == 0x2F800 else f"audio_pic_{index:02d}_{delta:05x}_"
        output = GENERATED / "spu" / prefix.rstrip("_")
        lift_relocated(original, delta, stage / name, output, prefix)
        phoff = struct.unpack_from(">I", original, 28)[0]
        phsize, phnum = struct.unpack_from(">HH", original, 42)
        loads = [struct.unpack_from(">8I", original, phoff+i*phsize) for i in range(phnum)]
        code = next(p for p in loads if p[0] == 1 and p[6] & 1)
        shoff = struct.unpack_from(">I", original, 32)[0]
        shsize, shnum = struct.unpack_from(">HH", original, 46)
        sections = [struct.unpack_from(">10I", original, shoff+i*shsize) for i in range(shnum)]
        code_end = max(s[3]+s[5] for s in sections if s[2] & 4)
        if code[2] != 0x80 or code[2]+code[5] > 0x3900:
            raise ValueError(f"Unvalidated audio plug-in allocation: {source}")
        image_id = 47 + ordinal
        fingerprint = fnv1a64(original[code[1]:code[1]+code[4]])
        declarations.append(f"extern void {prefix}spu_recomp_register(void);")
        registrations.extend([
            f"    spu_begin_image({image_id});",
            f"    {prefix}spu_recomp_register();",
            f"    spu_overlay_register_fingerprint_at(0x{fingerprint:016X}ULL, {code[4]}, {code_end-code[2]}, 0x{delta+code[2]:X}, {image_id});",
        ])
        provenance.append({"source": source.name, "sha256": hashlib.sha256(original).hexdigest(),
                           "relocation_base": hex(delta), "image_id": image_id})
    (GENERATED / "spu/audio_plugins_register.c").write_text(
        "/* Generated from locally supplied game files. */\n"
        '#include "spu_overlay_registry.h"\nextern void spu_begin_image(int);\n'
        + "\n".join(declarations)
        + "\n__attribute__((constructor)) static void haze_audio_plugins_register(void) {\n"
        + "\n".join(registrations) + "\n    spu_begin_image(0);\n}\n",
        encoding="utf-8", newline="\n")
    (stage / "provenance.json").write_text(json.dumps(provenance, indent=2)+"\n", encoding="utf-8")
    print(f"Translated {len(allocations)} local audio plug-in allocations")


if __name__ == "__main__":
    main()
