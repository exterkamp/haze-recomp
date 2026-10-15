import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

spec=importlib.util.spec_from_file_location('analysis',Path(__file__).resolve().parents[1]/'scripts/analyze_job13_capture.py')
A=importlib.util.module_from_spec(spec);spec.loader.exec_module(A)


def fixture(changed=False):
    fields=dict.fromkeys(A.FIELDS,0)
    fields.update(version=2,header_size=128,valid=1,image_id=32,final_pc=0xce04,
        event_count=3,event_calls=3,payload_size=12,qpc_frequency=1000,begin_qpc=100,end_qpc=101)
    header=A.HEADER.pack(b'HZJOB13\0',*(fields[k] for k in A.FIELDS))
    events=b''
    for i,cmd in enumerate((0x40,0x20,0x40)):
        events+=A.EVENT.pack(i,1,21,cmd,0,0,0x1000,4,0,i*4,4,struct.pack('<4I',cmd,0,0,0),0)
    return header+bytes(A.FIXED_BYTES-128)+events+b'abcdWXYZ'+(b'WXY!' if changed else b'WXYZ')


class TranscriptTests(unittest.TestCase):
    def analyze(self,data):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'fixture.bin';p.write_bytes(data);return A.analyze(p)

    def test_put_changes_later_get_without_external_mutation(self):
        r=self.analyze(fixture())
        self.assertTrue(r['accepted']);self.assertTrue(r['sequential_memory_consistent'])
        self.assertEqual(r['initial_known_bytes'],4)
        self.assertEqual((r['gets'],r['puts']),(2,1))

    def test_changed_read_is_not_claimed_owned(self):
        r=self.analyze(fixture(True))
        self.assertFalse(r['sequential_memory_consistent'])
        self.assertEqual(r['externally_changed_get_bytes'],1)

    def test_invalid_capture_never_accepted(self):
        data=bytearray(fixture());struct.pack_into('<I',data,16,0)
        self.assertFalse(self.analyze(data)['accepted'])

    def test_malformed_ranges_and_truncation_rejected(self):
        good=fixture()
        for data in (good[:-1],good+b'x',good[:40]):
            with self.assertRaises(ValueError):self.analyze(data)
        for offset,value in ((A.FIXED_BYTES,1),(A.FIXED_BYTES+36,12),(A.FIXED_BYTES+16,262144)):
            data=bytearray(good);struct.pack_into('<I',data,offset,value)
            with self.assertRaises(ValueError):self.analyze(data)

    def test_atomic_failure_does_not_publish_attempted_bytes(self):
        # SDK masks LS address, aligns low EA, ignores staged size/high EA.
        fields=dict.fromkeys(A.FIELDS,0)
        fields.update(version=2,header_size=128,valid=1,image_id=32,final_pc=0xce04,
            event_count=9,event_calls=9,payload_size=6*128,qpc_frequency=1000,begin_qpc=100,end_qpc=101)
        header=A.HEADER.pack(b'HZJOB13\0',*(fields[k] for k in A.FIELDS))
        events=[];payload=b''
        for cmd,status,blob in ((0xd0,4,b'a'*128),(0xb4,1,b'b'*128),
                               (0xd0,4,b'a'*128),(0xb4,0,b'c'*128),
                               (0xd0,4,b'c'*128),(0xb4,0,b'd'*128)):
            i=len(events)
            events.append(A.EVENT.pack(i,1,21,cmd,262144+17,99,0x107f,3,0,
                len(payload),128,struct.pack('<4I',cmd,0,0,0),status))
            payload+=blob
            if cmd==0xb4:
                events.append(A.EVENT.pack(len(events),2,27,0,0,0,0,0,0,
                    0xffffffff,0,struct.pack('<4I',status,0,0,0),0))
        data=header+bytes(A.FIXED_BYTES-128)+b''.join(events)+payload
        r=self.analyze(data)
        self.assertTrue(r['accepted']);self.assertTrue(r['sequential_memory_consistent'])
        self.assertEqual(r['atomic_failed_put_attempts'],1)
        self.assertEqual((r['gets'],r['puts']),(3,2))
        # Captured PUT says failure but read says success: reject inconsistent evidence.
        damaged=bytearray(data)
        struct.pack_into('<I',damaged,A.FIXED_BYTES+2*64+44,0)
        with self.assertRaises(ValueError):self.analyze(damaged)
        # Actual span crosses LS boundary, independent of the staged size3.
        damaged=bytearray(data)
        struct.pack_into('<I',damaged,A.FIXED_BYTES+16,262143)
        with self.assertRaises(ValueError):self.analyze(damaged)

    def test_old_schema_and_unrelated_status_rejected(self):
        data=bytearray(fixture());struct.pack_into('<I',data,8,1)
        with self.assertRaises(ValueError):self.analyze(data)
        data=bytearray(fixture());struct.pack_into('<I',data,A.FIXED_BYTES+60,4)
        with self.assertRaises(ValueError):self.analyze(data)


if __name__=='__main__':unittest.main()
