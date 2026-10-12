"""Reject invalid benchmark windows while retaining every stall in valid ones."""
import contextlib
import csv
import io
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from benchmark_present_modes import report


class PresentWindows(unittest.TestCase):
    def test_native_queue_mode_evidence(self):
        with tempfile.TemporaryDirectory(dir=ROOT / 'local') as folder:
            root = Path(folder)
            args = SimpleNamespace(csv=root/'p.csv', windows=root/'w.json',
                output=root/'r.json', min_frames=60, queue_mode_log=root/'native.log',
                queue_wait_tid=[37, 1])
            args.windows.write_text(json.dumps(dict(complete=True, qpc_frequency=1000,
                mode_column='spurs_queue_wait', blocks=[dict(flags=1,start_qpc=100,end_qpc=3000)])))
            with args.csv.open('w', newline='') as file:
                file.write('# qpc_frequency=1000\npresent_id,guest_frame,qpc\n')
                for frame in range(101): file.write(f'{frame+1},{frame},{frame*40}\n')
            good = ('[spurs-queue-wait-mode] tid=37 native=1 qpc=80\n'
                    '[spurs-queue-wait-mode] tid=1 native=1 qpc=90\n')
            def run(log):
                args.queue_mode_log.write_text(log)
                with contextlib.redirect_stdout(io.StringIO()): report(args)
            run(good)
            self.assertGreaterEqual(json.loads(args.output.read_text())['blocks'][0]['frames'],60)
            for invalid in ['', good.replace('tid=37','tid=38'),
                good.replace('qpc=80','qpc=101'), good.replace('native=1','native=0'),
                good+'[spurs-queue-wait-mode] tid=37 native=0 qpc=150\n'
                     '[spurs-queue-wait-mode] tid=37 native=1 qpc=160\n']:
                with self.subTest(log=invalid), self.assertRaises(ValueError): run(invalid)

    def test_modes_and_invalid_windows(self):
        with tempfile.TemporaryDirectory(dir=ROOT / 'local') as folder:
            root = Path(folder)
            args = SimpleNamespace(csv=root/'p.csv', windows=root/'w.json',
                                   output=root/'r.json', min_frames=60)
            for column, modes in [('cpu_work', (259,1283)), ('shadow_compare',(0,1)), ('depth_export',(0,1)), ('window_position',(0,2))]:
                with self.subTest(column=column):
                    rows, ticks = [], 0
                    for i in range(202):
                        ticks += 120 if i == 50 else 40
                        row = dict(present_id=i+1, guest_frame=i, qpc=ticks,
                                   cpu_work=259, shadow_compare=1, depth_export=1, window_position=1)
                        row[column] = modes[i>100]
                        rows.append(row)
                    windows = dict(complete=True, qpc_frequency=1000, mode_column=column,
                        blocks=[dict(flags=modes[0], start_qpc=rows[0]['qpc'], end_qpc=rows[100]['qpc']),
                                dict(flags=modes[1], start_qpc=rows[101]['qpc'], end_qpc=rows[201]['qpc'])])

                    def run():
                        args.windows.write_text(json.dumps(windows))
                        with args.csv.open('w',newline='') as stream:
                            stream.write('# qpc_frequency=1000\n')
                            writer=csv.DictWriter(stream,fieldnames=rows[0].keys())
                            writer.writeheader();writer.writerows(rows)
                        with contextlib.redirect_stdout(io.StringIO()):
                            report(args)

                    run()
                    blocks=json.loads(args.output.read_text())['blocks']
                    self.assertEqual([b['frames'] for b in blocks],[100,100])
                    self.assertEqual(blocks[0]['frame_ms']['max'],120)
                    self.assertEqual(blocks[0]['frames_over_50_ms'],1)
                    rows[50][column]=modes[1]
                    with self.assertRaises(ValueError):run()
                    rows[50][column]=modes[0];rows[50]['present_id']=999
                    with self.assertRaises(ValueError):run()
                    rows[50]['present_id']=51;windows['complete']=False
                    with self.assertRaises(ValueError):run()


if __name__=='__main__':unittest.main()
