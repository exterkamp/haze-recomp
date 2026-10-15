from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import patch_native_render_parameters as native


class NativeParameterPatch(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source = ROOT / 'generated/ppu/ppu_recomp_003.cpp'
        if not source.exists():
            for checkout in ROOT.parents:
                candidate = checkout / 'generated/ppu/ppu_recomp_003.cpp'
                if candidate.exists():
                    source = candidate
                    break
        if not source.exists():
            raise unittest.SkipTest('requires private generated Haze source')
        cls.source = source.read_text(encoding='cp1252')

    def test_only_audited_callsite_changes_and_original_remains(self):
        patched = native.patch(self.source)
        self.assertEqual(patched.count(native.REPLACEMENT), 1)
        self.assertEqual(native.audited_body(patched, '00673750'),
                         native.audited_body(self.source, '00673750'))
        restored = patched.replace('#include "haze_native_render_parameters.h"\n', '')
        self.assertEqual(restored.replace(native.REPLACEMENT, native.CALL), self.source)

    def test_guards_reject_changed_callee_caller_and_double_patch(self):
        for address in native.DIGESTS:
            body = native.audited_body(self.source, address)
            with self.assertRaises(ValueError):
                native.patch(self.source.replace(body, body.replace('vm_read32', 'changed_read32', 1)))
        with self.assertRaises(ValueError):
            native.patch(native.patch(self.source))

    def test_composes_with_existing_build_source(self):
        import patch_producer_profile
        # Actual order is producer instrumentation, then native parameters.
        # Removing just the native call replacement restores every timing scope
        # and every guest operation, including the original fallback function.
        profiled = patch_producer_profile.patch(self.source)
        patched = native.patch(profiled)
        restored = patched.replace('#include "haze_native_render_parameters.h"\n', '')
        self.assertEqual(restored.replace(native.REPLACEMENT, native.CALL), profiled)
        self.assertEqual(patch_producer_profile.strip_profile(profiled), self.source)
        self.assertEqual(patched.count(native.REPLACEMENT), 1)
        body = native.audited_body(profiled, '006740E0')
        changed = body.replace('vm_read32', 'changed_read32', 1)
        with self.assertRaisesRegex(ValueError, '006740E0 changed'):
            native.patch(profiled.replace(body, changed, 1))
        with self.assertRaises(ValueError):
            native.patch(patched)


if __name__ == '__main__':
    unittest.main()
