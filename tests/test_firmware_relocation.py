"""Asset-free ELF relocation: preserve code, relocate allocated symbols only."""
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("lift_firmware", Path(__file__).resolve().parents[1] / "scripts/lift_firmware.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def fixture():
    data = bytearray(384)
    data[:6] = b"\x7fELF\x01\x02"
    struct.pack_into(">H", data, 18, 23)
    struct.pack_into(">III", data, 24, 0x90, 52, 128)
    struct.pack_into(">HHHH", data, 42, 32, 1, 40, 3)
    struct.pack_into(">8I", data, 52, 1, 256, 0x80, 0x80, 32, 48, 5, 16)
    struct.pack_into(">10I", data, 168, 0, 1, 6, 0x80, 256, 32, 0, 0, 16, 0)
    struct.pack_into(">10I", data, 208, 0, 2, 0, 0, 288, 32, 0, 0, 4, 16)
    data[256:288] = bytes(range(32))
    struct.pack_into(">IIIBBH", data, 288, 0, 0x90, 4, 2, 0, 1)
    struct.pack_into(">IIIBBH", data, 304, 0, 0x1234, 0, 0, 0, 0xFFF1)  # ABS symbol.
    return bytes(data)


class RelocationTests(unittest.TestCase):
    def test_indirect_entries_discovered_before_relocation(self):
        # Synthetic ILA r2,0x90; BI r2 exports another routine at 0x90. The
        # address-valued seed no longer lies in .text after metadata relocation.
        data = bytearray(fixture())
        struct.pack_into(">I", data, 24, 0x80)
        struct.pack_into(">H", data, 48, 2)
        struct.pack_into(">II", data, 68, 20, 20)
        struct.pack_into(">I", data, 188, 20)
        struct.pack_into(">5I", data, 256, 0x42004802, 0x35000100,
                         0x40200000, 0x40200000, 0x35000000)
        with tempfile.TemporaryDirectory() as temp:
            stage = Path(temp) / "indirect"
            output = Path(temp) / "lifted"
            module.lift_relocated(bytes(data), 0x33100, stage, output, "fixture_")
            generated = (output / "spu_recomp.c").read_text(encoding="utf-8")
            self.assertIn("void fixture_spu_func_00033190(spu_context* ctx)", generated)
            self.assertEqual(stage.with_suffix(".code.bin").read_bytes(), bytes(data[256:276]))

    def test_metadata_and_code(self):
        original = fixture()
        result = module.relocate_elf(original, 0x33100)
        u32 = lambda pos: struct.unpack_from(">I", result, pos)[0]
        self.assertEqual(u32(24), 0x33190)
        self.assertEqual((u32(60), u32(64)), (0x33180, 0x33180))
        self.assertEqual(u32(180), 0x33180)
        self.assertEqual(u32(292), 0x33190)
        self.assertEqual(u32(308), 0x1234)
        self.assertEqual(result[256:288], original[256:288])

    def test_truncated_and_outside_local_store(self):
        for original, delta in [(fixture()[:52], 0), (fixture(), 0x40000), (b"bad", 0)]:
            with self.assertRaises(ValueError):
                module.relocate_elf(original, delta)


if __name__ == "__main__":
    unittest.main()
