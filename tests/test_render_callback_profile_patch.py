"""Guard the two-branch callback transform and title-prompt composition."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import patch_render_callback_profile as callback
import patch_title_prompt


def elf_function(path, address, size):
    """Read just the requested PPC64 BE executable bytes, using stdlib only."""
    with path.open("rb") as file:
        header = file.read(64)
        if header[:6] != b"\x7fELF\x02\x02" or struct.unpack_from(">H", header, 18)[0] != 21:
            raise ValueError("expected big-endian PPC64 ELF")
        phoff = struct.unpack_from(">Q", header, 32)[0]
        phentsize, phnum = struct.unpack_from(">HH", header, 54)
        for index in range(phnum):
            file.seek(phoff+index*phentsize)
            segment = file.read(phentsize)
            kind, flags = struct.unpack_from(">II", segment)
            offset, vaddr = struct.unpack_from(">QQ", segment, 8)
            filesz = struct.unpack_from(">Q", segment, 32)[0]
            if kind == 1 and flags & 1 and vaddr <= address and address+size <= vaddr+filesz:
                file.seek(offset+address-vaddr)
                return file.read(size)
    raise ValueError("function bytes missing from ELF executable segments")


class CallbackGuardTests(unittest.TestCase):
    def test_unpinned_body_is_rejected_without_private_assets(self):
        with self.assertRaisesRegex(ValueError, "callback 004D2828 changed"):
            callback.patch(callback.FUNCTION+"\n}\n")


class CallbackSourceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source_path = ROOT / "generated/ppu/ppu_recomp_002.cpp"
        if not cls.source_path.is_file():
            raise unittest.SkipTest("requires the locally generated BLUS30094 PPU lift")
        cls.source = cls.source_path.read_text(encoding="cp1252")

    def test_original_and_title_prompt_pipeline_strip_exactly(self):
        self.assertEqual(callback.strip_profile(callback.patch(self.source)), self.source)
        title = patch_title_prompt.patch(self.source)
        instrumented = callback.patch(title)
        self.assertEqual(callback.strip_profile(instrumented), title)
        # Hooks affect distinct functions; composition order has no guest-code effect.
        self.assertEqual(patch_title_prompt.patch(callback.patch(self.source)), instrumented)

    def test_two_dynamic_targets_keep_guest_call_toc_and_trampoline_sequence(self):
        before = callback.function_body(self.source, callback.FUNCTION)
        result = callback.patch(self.source)
        after = callback.function_body(result, callback.FUNCTION)
        self.assertEqual(after.count("haze_producer_profile::call_scope"), 2)
        self.assertNotIn("frame_scope", after)
        self.assertNotIn("haze_producer_frame", after)
        for lr in callback.INDIRECT_RETURNS:
            self.assertIn(f"haze_producer_indirect_{lr}(0x{lr}u, static_cast<std::uint32_t>(ctx->ctr));", after)
        self.assertEqual(before.count("ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);"), 2)
        self.assertEqual(after.count("ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);"), 2)
        self.assertEqual(after.count('ctx->gpr[2] = 0x00E94EE0ULL; /*TOCFIX ld r2,N(r1)*/'), 2)
        # Whole-source stripping also checks every descriptor load, register
        # operation, branch, call, return and TOC restore remains identical.
        self.assertEqual(callback.strip_profile(result), self.source)

    def test_mutated_repeated_missing_or_duplicate_body_rejected(self):
        body = callback.function_body(self.source, callback.FUNCTION)
        changed = body.replace("0x44", "0x48", 1)
        with self.assertRaisesRegex(ValueError, "changed"):
            callback.patch(self.source.replace(body, changed, 1))
        with self.assertRaisesRegex(ValueError, "changed"):
            callback.patch(callback.patch(self.source))
        with self.assertRaisesRegex(ValueError, "exactly one"):
            callback.patch(self.source.replace(callback.FUNCTION, "void removed_callback(ppu_context* ctx) {", 1))
        with self.assertRaisesRegex(ValueError, "exactly one"):
            callback.patch(self.source+"\n"+body)
        with self.assertRaisesRegex(ValueError, "changed"):
            callback.strip_profile(callback.patch(self.source).replace("0x004D28C4u", "0x004D28C8u"))

    def test_elf_bctrl_return_addresses_and_selector_branch(self):
        elf = ROOT / "game/disc/PS3_GAME/USRDIR/EBOOT.ELF"
        if not elf.is_file():
            self.skipTest("requires the private decrypted ELF for architectural branch validation")
        start = 0x004D2828
        code = elf_function(elf, start, 0xB4)
        found = [start+index+4 for index in range(0, len(code), 4)
                 if code[index:index+4] == bytes.fromhex("4e800421")]
        self.assertEqual(found, [int(lr, 16) for lr in callback.INDIRECT_RETURNS])
        # bne cr7 at 004D2864 selects the second path at 004D28A0;
        # the first bctrl belongs to r3==0, the second to r3!=0.
        self.assertEqual(code[0x4D2864-start:0x4D2868-start], bytes.fromhex("409e003c"))

    def test_cli_cp1252_input_and_inplace_second_transform(self):
        # Preserve private generated copies under local; no recursive cleanup.
        directory = Path(tempfile.mkdtemp(prefix="render-callback-profile-", dir=ROOT / "local"))
        source = directory / "ppu_recomp_002.cpp"
        title = patch_title_prompt.patch(self.source)+"\n// cp1252 roundtrip: \u20ac\n"
        source.write_text(title, encoding="cp1252")
        result = subprocess.run([sys.executable, str(ROOT / "scripts/patch_render_callback_profile.py"),
                                 str(source), str(source)], capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(callback.strip_profile(source.read_text(encoding="cp1252")), title)

    def test_cmake_title_command_runs_callback_second_and_tracks_both_scripts(self):
        cmake = (ROOT / "port/CMakeLists.txt").read_text()
        block = cmake.split('set(HAZE_TITLE_SOURCE "${RECOMP_DIR}/ppu_recomp_002.cpp")', 1)[1].split('\nendif()', 1)[0]
        title_command = 'COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/../scripts/patch_title_prompt.py"'
        callback_command = 'COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/../scripts/patch_render_callback_profile.py"'
        self.assertLess(block.index(title_command), block.index(callback_command))
        self.assertIn('"${HAZE_TITLE_PATCHED}" "${HAZE_TITLE_PATCHED}"', block)
        depends = block.split('DEPENDS', 1)[1]
        for script in ('patch_title_prompt.py', 'patch_render_callback_profile.py', 'patch_producer_profile.py'):
            self.assertIn(script, depends)


if __name__ == "__main__":
    unittest.main()
