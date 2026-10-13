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


def write_capture(root, events, saved, overwritten, callbacks, threshold=1000):
    source, windows, log = (root / name for name in ('draw.csv', 'windows.json', 'stderr.log'))
    rows = []
    for callback_id in events:
        event = row()
        event.update(callback_id=callback_id,
                     qpc_begin=callback_id * 30_000,
                     qpc_end=callback_id * 30_000 + 30_000,
                     threshold_us=threshold)
        rows.append(event)
    with source.open('w', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=rows[0])
        writer.writeheader()
        writer.writerows(rows)
    windows.write_text(json.dumps(dict(
        complete=True, qpc_frequency=10_000_000,
        blocks=[dict(start_qpc=0, end_qpc=(max(events) + 1) * 30_000 + 1, flags=1)])))
    log.write_text(f'[slow-draw] saved={saved} overwritten={overwritten} '
                   f'callbacks={callbacks} threshold_us={threshold}')
    return source, windows, log


class SlowDrawAnalysisTests(unittest.TestCase):
    def test_partition_and_raw_cycle_units(self):
        self.assertEqual(validate_rows([row()]), 10_000_000)

    def test_nonconserved_stage_rejected(self):
        bad = row(); bad[STAGES[0]] += 1
        with self.assertRaisesRegex(ValueError, 'partition'):
            validate_rows([bad])

    def test_nested_get_pso_detail_and_lookup_outcomes(self):
        for outcome in (0, 1, 2):
            detail = row()
            detail.update(get_pso_ms=0.4, pso_lookup=outcome,
                          build_pso_ms=0.3, shader_disk_load_ms=0.1,
                          d3d_compile_ms=0.1, create_graphics_pso_ms=0.1)
            validate_rows([detail])
        too_long = row()
        too_long.update(get_pso_ms=0.500004, pso_lookup=1)
        with self.assertRaisesRegex(ValueError, 'exceeds pipeline'):
            validate_rows([too_long])
        nonfinite = row()
        nonfinite.update(get_pso_ms=float('nan'), pso_lookup=2)
        with self.assertRaisesRegex(ValueError, 'nested get_pso'):
            validate_rows([nonfinite])
        for bad in ({'get_pso_ms': 0.1}, {'pso_lookup': 1},
                    {'get_pso_ms': 0.1, 'pso_lookup': 3}):
            detail = row(); detail.update(bad)
            with self.assertRaises(ValueError):
                validate_rows([detail])
        phase_exceeds_build = row()
        phase_exceeds_build.update(get_pso_ms=0.4, pso_lookup=2,
                                   build_pso_ms=0.2, shader_disk_load_ms=0.1,
                                   d3d_compile_ms=0.1, create_graphics_pso_ms=0.1)
        with self.assertRaisesRegex(ValueError, 'exceed build_pso'):
            validate_rows([phase_exceeds_build])
        partial_phases = row()
        partial_phases.update(get_pso_ms=0.4, pso_lookup=0, build_pso_ms=0.2)
        with self.assertRaisesRegex(ValueError, 'Incomplete nested PSO phase'):
            validate_rows([partial_phases])

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

    def test_analyze_reads_full_pso_schema_and_legacy_schema(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, windows, log = (root / name for name in ('draw.csv', 'windows.json', 'stderr.log'))
            windows.write_text(json.dumps(dict(
                complete=True, qpc_frequency=10_000_000,
                blocks=[dict(start_qpc=0, end_qpc=50_000, flags=1)])))
            log.write_text('[slow-draw] saved=1 overwritten=0 callbacks=1 threshold_us=1000')

            full = row()
            full.update(threshold_us=1000, get_pso_ms=0.4, pso_lookup=2,
                        build_pso_ms=0.3, shader_disk_load_ms=0.1,
                        d3d_compile_ms=0.1, create_graphics_pso_ms=0.1)
            with source.open('w', newline='') as stream:
                writer = csv.DictWriter(stream, fieldnames=full)
                writer.writeheader()
                writer.writerow(full)
            report = analyze(source, windows, log)
            self.assertEqual(report['blocks'][0]['retained_full_callbacks'], 1)
            with source.open(newline='') as stream:
                parsed = next(csv.DictReader(stream))
            self.assertEqual(float(parsed['shader_disk_load_ms']), 0.1)

            legacy = row()
            legacy.update(threshold_us=1000)
            with source.open('w', newline='') as stream:
                writer = csv.DictWriter(stream, fieldnames=legacy)
                writer.writeheader()
                writer.writerow(legacy)
            report = analyze(source, windows, log)
            self.assertEqual(report['blocks'][0]['retained_full_callbacks'], 1)

    def test_legacy_256_capacity_default_remains_compatible(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, windows, log = write_capture(root, range(3, 259), 256, 2, 258)
            report = analyze(source, windows, log)
            self.assertEqual(report['capacity'], 256)
            self.assertEqual((report['saved'], report['overwritten']), (256, 2))

    def test_capacity_1024_no_overwrite_and_full_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, windows, log = write_capture(root, range(1, 4), 3, 0, 3)
            report = analyze(source, windows, log, capacity=1024)
            self.assertEqual((report['capacity'], report['saved'], report['overwritten']),
                             (1024, 3, 0))

            source, windows, log = write_capture(root, range(5, 1029), 1024, 4, 1028)
            report = analyze(source, windows, log, capacity=1024)
            self.assertEqual((report['capacity'], report['saved'], report['overwritten']),
                             (1024, 1024, 4))

    def test_ring_counts_must_match_selected_capacity(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, windows, log = write_capture(root, range(1, 1025), 1024, 4, 1027)
            with self.assertRaisesRegex(ValueError, 'counts'):
                analyze(source, windows, log, capacity=1024)

            source, windows, log = write_capture(root, range(5, 1028), 1023, 4, 1028)
            with self.assertRaisesRegex(ValueError, 'not full'):
                analyze(source, windows, log, capacity=1024)

            source, windows, log = write_capture(root, range(1, 3), 2, 0, 1)
            with self.assertRaisesRegex(ValueError, 'counts'):
                analyze(source, windows, log, capacity=1024)

    def test_capacity_must_be_positive_integer(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, windows, log = write_capture(root, range(1, 2), 1, 0, 1)
            for capacity in (0, -1, 1.5, True):
                with self.assertRaisesRegex(ValueError, 'positive integer'):
                    analyze(source, windows, log, capacity=capacity)


if __name__ == '__main__':
    unittest.main()
