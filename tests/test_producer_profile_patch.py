"""Guard the generated main-producer call timing transform."""

from collections import Counter
from pathlib import Path
import sys
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import patch_guest_pacer
import patch_producer_profile as producer_profile
import patch_job_wait
import patch_vp_upload
import patch_job_dispatch_trace
import patch_slot_copy
import patch_save_status
import patch_audio_tick
import patch_movie_poll


class ProducerProfilePatchTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source_path = ROOT / "generated/ppu/ppu_recomp_003.cpp"
        cls.memmove_path = ROOT / "generated/ppu/ppu_recomp_005.cpp"
        # Native-render diagnostic worktrees share the private generated lift
        # from their enclosing checkout rather than copying guest content.
        if not cls.source_path.exists():
            for checkout in ROOT.parents:
                candidate = checkout / "generated/ppu/ppu_recomp_003.cpp"
                if candidate.exists():
                    cls.source_path = candidate
                    cls.memmove_path = checkout / "generated/ppu/ppu_recomp_005.cpp"
                    break
        if not cls.source_path.exists() or not cls.memmove_path.exists():
            raise unittest.SkipTest("Requires the locally generated BLUS30094 PPU lift")
        cls.source = cls.source_path.read_text(encoding="cp1252")

    def test_pinned_original_and_guest_pacer_pipeline_are_accepted(self):
        original = producer_profile.patch(self.source)
        self.assertEqual(producer_profile.strip_profile(original), self.source)

        # Replay the actual generated-source pipeline through slot-copy, which
        # is the build stage immediately before this diagnostic.
        paced = patch_guest_pacer.patch(self.source)
        waited = patch_job_wait.patch(paced)
        vp_patched = patch_vp_upload.patch(waited)
        traced = patch_job_dispatch_trace.patch(vp_patched)
        slot_copy = patch_slot_copy.patch(traced, self.memmove_path.read_text(encoding="cp1252"))
        instrumented = producer_profile.patch(slot_copy)
        self.assertEqual(producer_profile.strip_profile(instrumented), slot_copy)
        self.assertIn('#include "haze_producer_profile.h"\n', instrumented)
        variant = producer_profile.function_body(slot_copy, "void func_0072C368(ppu_context* ctx) {")
        self.assertEqual(producer_profile._digest(variant+"\n"),
                         producer_profile.RENDER_CALLBACK_VARIANT_SHA256["0072C368"])
        changed = variant.replace("haze_vp_upload_begin(ctx)", "changed_vp_upload_begin(ctx)")
        with self.assertRaisesRegex(ValueError, "render callback 0072C368 changed"):
            producer_profile.patch(slot_copy.replace(variant, changed, 1))

        # Main now composes save/audio/movie hooks after the performance chain.
        # Removing only profiler scopes must yield the identical hooked game.
        final = patch_movie_poll.patch(patch_audio_tick.patch(patch_save_status.patch(instrumented)))
        reference = patch_movie_poll.patch(patch_audio_tick.patch(patch_save_status.patch(slot_copy)))
        self.assertEqual(producer_profile.strip_profile(final), reference)

    def test_every_audited_direct_call_is_covered_or_explicitly_excluded(self):
        source = patch_guest_pacer.patch(self.source)
        result = producer_profile.patch(source)
        body = producer_profile.function_body(result)
        sites = producer_profile.call_sites(body)
        self.assertEqual(len(sites), 74)
        scopes = [line for line in body.splitlines()
                  if "haze_producer_profile::call_scope haze_producer_call_" in line]
        self.assertEqual(len(scopes), 59)
        scope_sites = []
        for line in scopes:
            tokens = line.split("haze_producer_frame, ", 1)[1].split(");", 1)[0]
            lr, callee = [int(part.strip().rstrip("u"), 16) for part in tokens.split(",")]
            scope_sites.append((lr, f"{callee:08X}"[-8:]))
        expected = Counter(
            (int(match.group("lr"), 16), match.group("callee").upper())
            for match in sites if match.group("callee").upper() not in producer_profile.EXCLUDED_CALLEES
        )
        self.assertEqual(Counter(scope_sites), expected)

        excluded_calls = [match.group("callee").upper() for match in sites
                          if match.group("callee").upper() in producer_profile.EXCLUDED_CALLEES]
        self.assertEqual(len(excluded_calls), 15)
        self.assertEqual(set(excluded_calls), producer_profile.EXCLUDED_CALLEES)
        self.assertTrue(all(
            f"func_{callee}(ctx); DRAIN_TRAMPOLINE(ctx);" in body
            for callee in ("00680D18", "0068EC78")))
        self.assertIn("func_0068ECD8(ctx); DRAIN_TRAMPOLINE(ctx);", body)
        self.assertIn("func_006DD638(ctx); DRAIN_TRAMPOLINE(ctx);", body)

    def test_changed_or_repeated_function_is_rejected(self):
        changed = self.source.replace("func_00680D18(ctx);", "func_00680D19(ctx);", 1)
        with self.assertRaisesRegex(ValueError, "changed"):
            producer_profile.patch(changed)
        with self.assertRaisesRegex(ValueError, "changed"):
            producer_profile.patch(producer_profile.patch(self.source))

    def test_dispatcher_and_dynamic_targets_preserve_calls(self):
        result = producer_profile.patch(self.source)
        self.assertEqual(producer_profile.strip_profile(result), self.source)
        main = producer_profile.function_body(result)
        nested = producer_profile.function_body(result, producer_profile.DISPATCH_FUNCTION)
        self.assertEqual(len(producer_profile.call_sites(nested)), 14)
        self.assertEqual(nested.count("haze_producer_nested_"), 14)
        self.assertEqual(nested.count("haze_producer_indirect_"), 4)
        self.assertEqual(main.count("haze_producer_indirect_"), 2)
        for body, returns in ((main, producer_profile.MAIN_INDIRECT_RETURNS),
                              (nested, producer_profile.DISPATCH_INDIRECT_RETURNS)):
            for lr in returns:
                self.assertIn(f"0x{lr}u, static_cast<std::uint32_t>(ctx->ctr));", body)
        original_body = producer_profile.function_body(self.source, producer_profile.DISPATCH_FUNCTION)
        changed = self.source.replace(original_body, original_body.replace("func_0068EC40(ctx)", "func_0068EC41(ctx)"))
        with self.assertRaisesRegex(ValueError, "dispatcher changed"):
            producer_profile.patch(changed)

    def test_hitch_phase_scopes_preserve_calls_and_reject_changed_code(self):
        result = producer_profile.patch(self.source)
        self.assertEqual(producer_profile.strip_profile(result), self.source)
        for address, _, count in producer_profile.PHASE_FUNCTIONS:
            name = f"void func_{address}(ppu_context* ctx) {{"
            before = producer_profile.function_body(self.source, name)
            after = producer_profile.function_body(result, name)
            calls = producer_profile.call_sites(before)
            self.assertEqual(len(calls), count)
            self.assertEqual(after.count("haze_producer_nested_"), count)
            self.assertEqual(after.count("ps3_indirect_call(ctx);"), before.count("ps3_indirect_call(ctx);"))
            returns = producer_profile.PHASE_INDIRECT_RETURNS.get(address, ())
            self.assertEqual(after.count("haze_producer_indirect_"), len(returns))
            for lr in returns:
                self.assertIn(f"0x{lr}u, static_cast<std::uint32_t>(ctx->ctr));", after)
            for match in calls:
                lr, callee = match.group("lr"), match.group("callee")
                self.assertIn(f"haze_producer_nested_{lr}(0x{lr}u, 0x{callee}u);", after)
            if calls:
                altered = before.replace(calls[0].group(0), calls[0].group(0).replace("ctx->lr", "ctx->ctr"), 1)
            else:
                self.assertTrue(returns, "a profiled phase must contain a call")
                altered = before.replace("ps3_indirect_call(ctx);", "changed_indirect_call(ctx);", 1)
            with self.assertRaisesRegex(ValueError, f"phase {address} changed"):
                producer_profile.patch(self.source.replace(before, altered, 1))

    def test_render_record_callback_targets_and_parent_paths_are_scoped(self):
        result = producer_profile.patch(self.source)
        self.assertEqual(producer_profile.strip_profile(result), self.source)

        # These scopes must execute inside the actual generated callback
        # functions. Their constructor reads CTR before the unchanged guest
        # trampoline call, preserving the dynamically resolved target.
        for address, _, returns in producer_profile.RENDER_CALLBACK_FUNCTIONS:
            before = producer_profile.function_body(
                self.source, f"void func_{address}(ppu_context* ctx) {{")
            body = producer_profile.function_body(
                result, f"void func_{address}(ppu_context* ctx) {{")
            self.assertEqual(body.count("haze_producer_indirect_"), len(returns))
            self.assertEqual(before.count("ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);"), len(returns))
            # No additional direct helper expansion in these callback bodies.
            self.assertNotIn("haze_producer_nested_", body)
            for return_lr in returns:
                scope = (f"haze_producer_indirect_{return_lr}(0x{return_lr}u, "
                         "static_cast<std::uint32_t>(ctx->ctr));")
                self.assertEqual(body.count(scope), 1)
                self.assertIn(
                    scope + "\n            ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);",
                    body)
            changed = before.replace("ps3_indirect_call(ctx);", "changed_indirect_call(ctx);", 1)
            with self.assertRaisesRegex(ValueError, f"render callback {address} changed"):
                producer_profile.patch(self.source.replace(before, changed, 1))

        # A computed switch-tail fallback is not a bctrl callback. Preserve its
        # guest dispatch and return without introducing a scope across it.
        switch_body = producer_profile.function_body(result, "void func_0072C368(ppu_context* ctx) {")
        self.assertIn("default: ps3_indirect_call(ctx); return; } return;", switch_body)

        # 007E41E8 -> 00676938 is explicitly parented, so the 006743F8
        # indirect target is distinguishable from 00675800 under 007E41E8.
        parent = producer_profile.function_body(
            result, "void func_007E41E8(ppu_context* ctx) {")
        self.assertEqual(parent.count(
            "haze_producer_nested_007E4974(0x007E4974u, 0x00676938u);"), 1)
        self.assertIn(
            "haze_producer_nested_007E4974(0x007E4974u, 0x00676938u);\n"
            "            ctx->lr = 0x007E4974; func_00676938(ctx); DRAIN_TRAMPOLINE(ctx);",
            parent)
        self.assertEqual(parent.count("haze_producer_nested_"), len(producer_profile.RENDER_PATH_CALLS))
        for lr, callee in producer_profile.RENDER_PATH_CALLS:
            self.assertEqual(parent.count(f"haze_producer_nested_{lr}(0x{lr}u, 0x{callee}u);"), 1)
            self.assertIn(f"ctx->lr = 0x{lr}; func_{callee}(ctx); DRAIN_TRAMPOLINE(ctx);", parent)

        # The direct route to 00675800 remains inside 007E41E8, where its
        # indirect scope inherits the active 007E41E8 call scope.
        self.assertIn("ctx->lr = 0x007E4520; func_00675800(ctx); DRAIN_TRAMPOLINE(ctx);",
                      producer_profile.function_body(
                          self.source, "void func_007E41E8(ppu_context* ctx) {"))

    def test_record_callback_return_is_the_elf_link_address(self):
        from tests.test_render_callback_profile_patch import elf_function
        elf = ROOT / "game/disc/PS3_GAME/USRDIR/EBOOT.ELF"
        if not elf.is_file():
            for checkout in ROOT.parents:
                candidate = checkout / "game/disc/PS3_GAME/USRDIR/EBOOT.ELF"
                if candidate.is_file():
                    elf = candidate
                    break
        if not elf.is_file():
            self.skipTest("requires private ELF")
        start = 0x006A8AE8
        code = elf_function(elf, start, 0x98)
        returns = [start+i+4 for i in range(0, len(code), 4)
                   if code[i:i+4] == bytes.fromhex("4e800421")]
        self.assertEqual(returns, [int(lr, 16) for lr in
                                  producer_profile.PHASE_INDIRECT_RETURNS["006A8AE8"]])

        expected_callbacks = dict(producer_profile.PHASE_INDIRECT_RETURNS)
        expected_callbacks.update((address, returns) for address, _, returns in
                                  producer_profile.RENDER_CALLBACK_FUNCTIONS)
        # Exact function extents prevent accidentally accepting a callback
        # belonging to the next function. Compare every bctrl in source order.
        for address, size in (
            ("00675800", 0x760), ("006743F8", 0x34C),
            ("00673AF0", 0x5F0), ("0072BAD0", 0x898), ("0072C368", 0xFF8),
        ):
            start = int(address, 16)
            code = elf_function(elf, start, size)
            returns = [start+i+4 for i in range(0, len(code), 4)
                       if code[i:i+4] == bytes.fromhex("4e800421")]
            self.assertEqual(returns, [int(lr, 16) for lr in expected_callbacks[address]], address)

        # The two complete state/material direct-call frontiers also use
        # architectural LR and target values, independently of lifted ctx->lr.
        for address, size in (("00675800", 0x760), ("006740E0", 0x21C), ("007E41E8", 0xDF0)):
            start = int(address, 16)
            code = elf_function(elf, start, size)
            body = producer_profile.function_body(self.source, f"void func_{address}(ppu_context* ctx) {{")
            for match in producer_profile.call_sites(body):
                if address == "007E41E8" and (match.group("lr"), match.group("callee")) not in producer_profile.RENDER_PATH_CALLS:
                    continue
                pc = int(match.group("lr"), 16) - 4
                word = int.from_bytes(code[pc-start:pc-start+4], "big")
                self.assertEqual(word >> 26, 18, f"expected branch at {pc:08X}")
                self.assertEqual(word & 1, 1, f"expected link at {pc:08X}")
                displacement = word & 0x03FFFFFC
                if displacement & 0x02000000:
                    displacement -= 0x04000000
                target = displacement if word & 2 else pc + displacement
                self.assertEqual(target & 0xFFFFFFFF, int(match.group("callee"), 16))


if __name__ == "__main__":
    unittest.main()
