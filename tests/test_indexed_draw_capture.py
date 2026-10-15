import importlib.util
import os
from pathlib import Path
import struct
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
import patch_indexed_draw_capture as patcher
from analyze_indexed_draw_capture import analyze,decode_stream

class IndexedCapture(unittest.TestCase):
    def test_actual_source_patch_and_stale_guard(self):
        source_path=Path(os.environ.get('HAZE_INDEXED_ORACLE_SOURCE',ROOT/'generated/ppu/ppu_recomp_003.cpp'))
        if not source_path.exists(): self.skipTest('requires the private generated Haze source')
        source=patcher.read_source(source_path);patched=patcher.patch(source)
        original=patcher.body(source)
        self.assertEqual(patched.count('haze_draw_capture.callback('),6)
        self.assertEqual(patched.count('haze_draw_capture.begin('),2)
        self.assertEqual(patched.count('haze_draw_capture.finish();'),1)
        restored='\n'.join(line for line in patched.split('\n') if 'haze_draw_capture' not in line and line!=patcher.INCLUDE.rstrip())
        self.assertEqual(restored,source)
        with self.assertRaises(ValueError): patcher.patch(patched)
        with self.assertRaises(ValueError): patcher.patch(source.replace(original,original+'\n/* drift */',1))

    def test_stream_validation(self):
        words=[0x41714,0,0x8181c,0x12340,17,0x41808,5,0x41824,0x02000000,0x41808,0]
        def decode(values): return decode_stream(struct.pack('>'+str(len(values))+'I',*values))
        self.assertEqual(decode(words)['count'],3)
        for index,value in [(0,0),(4,33),(6,0),(8,0x02000001),(9,0x41714)]:
            changed=words.copy();changed[index]=value
            with self.assertRaises(ValueError): decode(changed)
        with self.assertRaises(ValueError): decode(words[:-1])

    def test_native_capture_boundaries(self):
        if 'HAZE_INDEXED_FIXTURE_CAPTURE' not in os.environ:
            self.skipTest('run through run_indexed_draw_contract.py for compiled recorder coverage')
        path=Path(os.environ['HAZE_INDEXED_FIXTURE_CAPTURE'])
        report=analyze(path)
        self.assertEqual(report['accepted'],1)
        self.assertEqual(report['rejected'],5)
        self.assertEqual([r['flags'] for r in report['rows']],[0,1,2,4,8,16])
        self.assertEqual(report['rows'][0]['callbacks_before'],1)
        self.assertEqual(report['rows'][2]['callbacks_after'],1)

if __name__=='__main__': unittest.main()
