import copy
import csv
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from analyze_slow_draws import STAGES, analyze, validate_rows


def row():
    return dict(callback_id=1, qpc_frequency=10_000_000, qpc_begin=10_000,
                qpc_end=40_000, wall_ms=3.0, thread_cycles_valid=1,
                thread_cycles_begin=100, thread_cycles_end=190,
                thread_cycles_delta_not_ms=90,
                **dict(zip(STAGES, (1.0, 0.5, 0.5, 1.0))))


class SlowDrawAnalysisTests(unittest.TestCase):
    def test_partition_and_raw_cycle_units(self):
        self.assertEqual(validate_rows([row()]), 10_000_000)

    def test_nonconserved_stage_rejected(self):
        bad = row(); bad[STAGES[0]] += 1
        with self.assertRaisesRegex(ValueError, 'partition'):
            validate_rows([bad])

    def test_nan_and_qpc_mismatch_rejected(self):
        for key, value in [('wall_ms', float('nan')), ('qpc_end', 50_000)]:
            bad = row(); bad[key] = value
            with self.assertRaises(ValueError): validate_rows([bad])

    def test_cycle_failures_cannot_report_cpu_work(self):
        for change in ({'thread_cycles_end': 99}, {'thread_cycles_delta_not_ms': 1},
                       {'thread_cycles_valid': 0}):
            bad = row(); bad.update(change)
            with self.assertRaises(ValueError): validate_rows([bad])
        invalid = row(); invalid.update(thread_cycles_valid=0, thread_cycles_delta_not_ms=0)
        validate_rows([invalid])

    def test_order_and_clock_changes_rejected(self):
        a, b = row(), copy.deepcopy(row())
        with self.assertRaisesRegex(ValueError, 'IDs'): validate_rows([a, b])
        b.update(callback_id=2, qpc_frequency=1_000_000)
        with self.assertRaisesRegex(ValueError, 'frequency'): validate_rows([a, b])

    def test_report_filters_windows_and_rejects_bad_capture_metadata(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, windows, log = (root / name for name in ('draw.csv', 'windows.json', 'stderr.log'))
            event = row()
            event.update(threshold_us=1000)
            with source.open('w', newline='') as stream:
                writer = csv.DictWriter(stream, fieldnames=event)
                writer.writeheader()
                writer.writerow(event)
            capture = dict(complete=True, qpc_frequency=10_000_000,
                           blocks=[dict(start_qpc=1, end_qpc=50_000, flags=1)])
            windows.write_text(json.dumps(capture))
            log.write_text('[slow-draw] saved=1 overwritten=0 callbacks=2 threshold_us=1000')
            report = analyze(source, windows, log)
            self.assertEqual(report['blocks'][0]['retained_full_callbacks'], 1)
            capture['blocks'][0]['start_qpc'] = 20_000
            windows.write_text(json.dumps(capture))
            report = analyze(source, windows, log)
            self.assertEqual(report['blocks'][0]['retained_full_callbacks'], 0)
            self.assertEqual(report['blocks'][0]['retained_edge_callbacks'], 1)
            capture['blocks'][0]['end_qpc'] = 20_000
            windows.write_text(json.dumps(capture))
            with self.assertRaisesRegex(ValueError, 'measurement window'):
                analyze(source, windows, log)
            log.write_text('[slow-draw] saved=2 overwritten=0 callbacks=2 threshold_us=1000')
            with self.assertRaisesRegex(ValueError, 'counts'):
                analyze(source, windows, log)


if __name__ == '__main__':
    unittest.main()
