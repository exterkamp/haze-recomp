import csv
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "analyze_taskset_scans", ROOT / "scripts" / "analyze_taskset_scans.py")
ANALYZER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ANALYZER)


FIELDS = [
    "ticket", "taskset_ea", "dispatch_cause", "host_tid", "begin_qpc", "end_qpc",
    "qpc_valid", "total_ticks", "prep_ticks", "scan_ticks", "total_thread_cycles",
    "prep_thread_cycles", "scan_thread_cycles", "total_cycles_valid",
    "prep_cycles_valid", "watch_ticks", "slots", "eligible", "claimed",
    "dispatch_attempts", "dispatch_ok", "dispatch_failed",
]


def row(ticket, begin, end, eligible, prep=2, total_cycles=90, prep_cycles=20,
        cause=3, tid=42, claimed=0, attempts=0, ok=0, failed=0):
    total = end - begin
    return {
        "ticket": ticket, "taskset_ea": "00001000", "dispatch_cause": cause,
        "host_tid": tid, "begin_qpc": begin, "end_qpc": end, "qpc_valid": 1,
        "total_ticks": total, "prep_ticks": prep, "scan_ticks": total - prep,
        "total_thread_cycles": total_cycles, "prep_thread_cycles": prep_cycles,
        "scan_thread_cycles": total_cycles - prep_cycles, "total_cycles_valid": 1,
        "prep_cycles_valid": 1, "watch_ticks": 0, "slots": 128,
        "eligible": eligible, "claimed": claimed, "dispatch_attempts": attempts,
        "dispatch_ok": ok, "dispatch_failed": failed,
    }


class TasksetScanAnalyzerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.scan = self.root / "scan.csv"
        self.windows = self.root / "windows.json"
        self.presents = self.root / "presents.csv"
        self.rows = [
            row(0, 110, 130, eligible=0, prep=2, total_cycles=100, prep_cycles=20),
            row(1, 120, 140, eligible=1, prep=4, total_cycles=200, prep_cycles=30,
                claimed=1, attempts=1, ok=1),
            row(2, 195, 205, eligible=0),  # partially outside measurement window
        ]
        self.write_capture()
        self.windows.write_text(json.dumps({
            "qpc_frequency": 1_000_000,
            "complete": True,
            "blocks": [{"start_qpc": 100, "end_qpc": 200, "camera_token": 17}],
        }), encoding="utf-8")
        self.presents.write_text(
            "# qpc_frequency=1000000\n"
            "present_id,guest_frame,qpc\n"
            "1,9,95\n2,10,125\n3,11,165\n4,12,205\n", encoding="utf-8")

    def tearDown(self):
        self.temp.cleanup()

    def write_capture(self, *, metadata=None, rows=None):
        defaults = {
            "qpc_frequency": 1_000_000, "capacity": 8, "next_ticket": 3,
            "overwritten": 0, "dropped_busy": 0, "dropped_stale": 0,
            "max_discarded_end_qpc": 0, "capture_start_qpc": 90,
            "capture_end_qpc": 210, "complete": 1, "inflight": 0,
            "retained_rows": 3, "failure_reason": "none", "discarded_qpc_invalid": 0,
        }
        metadata = {**defaults, **(metadata or {})}
        with self.scan.open("w", newline="", encoding="utf-8") as stream:
            stream.write("#" + ",".join(f"{key},{value}" for key, value in metadata.items()) + "\n")
            writer = csv.DictWriter(stream, fieldnames=FIELDS)
            writer.writeheader()
            writer.writerows(self.rows if rows is None else rows)

    def test_window_excludes_boundary_scans_and_reports_wall_sums_and_call_union(self):
        report = ANALYZER.analyze(self.scan, self.windows, self.presents)
        window = report["windows"][0]
        self.assertEqual(window["counts"]["whole_scans"], 2)
        self.assertEqual(window["counts"]["empty"], 1)
        self.assertEqual(window["counts"]["nonempty"], 1)
        self.assertEqual(window["coverage"]["boundary_scans_excluded"], 1)
        # The two scan phases have 18 and 16 ticks; no phase-union placement is inferred.
        self.assertEqual(window["wall"]["scan_sum_ms"], 0.034)
        # Whole-call intervals [110,130] and [120,140] union to 30 ticks.
        self.assertEqual(window["wall"]["whole_call_union_occupancy_ms"], 0.03)
        self.assertNotIn("scan_union_occupancy_ms", window["wall"])
        self.assertEqual(window["wall"]["scan_sum_ms_per_present"], 0.034)
        self.assertEqual(window["thread_cycles"]["scan_raw_sum"], 250)
        self.assertIn("raw QueryThreadCycleTime", window["thread_cycles"]["unit"])

    def test_retention_gap_or_duplicate_ticket_rejected(self):
        self.write_capture(rows=[self.rows[0], self.rows[2]])
        with self.assertRaisesRegex(ValueError, "retained row count"):
            ANALYZER.analyze(self.scan, self.windows, self.presents)
        duplicate = [self.rows[0], self.rows[0], self.rows[2]]
        self.write_capture(rows=duplicate)
        with self.assertRaisesRegex(ValueError, "tickets"):
            ANALYZER.analyze(self.scan, self.windows, self.presents)

    def test_discard_overlap_and_retained_ticket_gap_are_rejected(self):
        meta = {"qpc_frequency": 1_000_000, "capacity": 8, "next_ticket": 3,
                "overwritten": 0, "dropped_busy": 0, "dropped_stale": 0,
                "max_discarded_end_qpc": 99, "capture_start_qpc": 90,
                "capture_end_qpc": 210, "complete": 1, "inflight": 0,
                "retained_rows": 3, "failure_reason": "none", "discarded_qpc_invalid": 0}
        self.write_capture(metadata=meta)
        report = ANALYZER.analyze(self.scan, self.windows, self.presents)
        coverage = report["windows"][0]["coverage"]
        self.assertTrue(coverage["ticket_accounting_complete"])
        meta["max_discarded_end_qpc"] = 101
        self.write_capture(metadata=meta)
        with self.assertRaisesRegex(ValueError, "discarded scan may overlap"):
            ANALYZER.analyze(self.scan, self.windows, self.presents)
        meta["dropped_busy"] = 1
        meta["max_discarded_end_qpc"] = 99
        meta["retained_rows"] = 2
        self.write_capture(metadata=meta, rows=[self.rows[0], self.rows[2]])
        report = ANALYZER.analyze(self.scan, self.windows, self.presents)
        self.assertEqual(report["windows"][0]["coverage"]["dropped_busy_total"], 1)
        meta["dropped_stale"] = 1  # unbalanced ticket accounting
        self.write_capture(metadata=meta, rows=[self.rows[0], self.rows[2]])
        with self.assertRaisesRegex(ValueError, "ticket allocations"):
            ANALYZER.analyze(self.scan, self.windows, self.presents)

    def test_incomplete_or_out_of_capture_window_rejected(self):
        meta = {"qpc_frequency": 1_000_000, "capacity": 8, "next_ticket": 3,
                "overwritten": 0, "dropped_busy": 0, "dropped_stale": 0,
                "max_discarded_end_qpc": 0, "capture_start_qpc": 90,
                "capture_end_qpc": 210, "complete": 0, "inflight": 1,
                "retained_rows": 0, "failure_reason": "writer_timeout",
                "discarded_qpc_invalid": 0}
        self.write_capture(metadata=meta, rows=[])
        with self.assertRaisesRegex(ValueError, "incomplete"):
            ANALYZER.analyze(self.scan, self.windows, self.presents)
        self.write_capture(metadata={**meta, "complete": 1, "inflight": 0,
                                     "failure_reason": "none", "capture_end_qpc": 199,
                                     "retained_rows": 3})
        with self.assertRaisesRegex(ValueError, "outside.*capture bounds"):
            ANALYZER.analyze(self.scan, self.windows, self.presents)

    def test_loss_without_valid_discard_timestamp_rejected(self):
        meta = {"overwritten": 1, "next_ticket": 4, "retained_rows": 3,
                "discarded_qpc_invalid": 1}
        self.write_capture(metadata=meta)
        with self.assertRaisesRegex(ValueError, "invalid QPC timing"):
            ANALYZER.analyze(self.scan, self.windows, self.presents)

    def test_bad_count_clock_or_cycle_reconciliation_rejected(self):
        bad = [dict(r) for r in self.rows]
        bad[1]["dispatch_ok"] = 0
        self.write_capture(rows=bad)
        with self.assertRaisesRegex(ValueError, "dispatch outcome"):
            ANALYZER.analyze(self.scan, self.windows, self.presents)
        bad = [dict(r) for r in self.rows]
        bad[0]["scan_thread_cycles"] += 1
        self.write_capture(rows=bad)
        with self.assertRaisesRegex(ValueError, "scan cycles"):
            ANALYZER.analyze(self.scan, self.windows, self.presents)
        bad = [dict(r) for r in self.rows]
        bad[0]["end_qpc"] += 1
        self.write_capture(rows=bad)
        with self.assertRaisesRegex(ValueError, "total ticks"):
            ANALYZER.analyze(self.scan, self.windows, self.presents)

    def test_address_is_hex_and_invalid_qpc_timing_is_rejected(self):
        _, parsed = ANALYZER._read_capture(self.scan)
        self.assertEqual(parsed[0]["taskset_ea"], 0x1000)
        bad = [dict(r) for r in self.rows]
        bad[0]["qpc_valid"] = 0
        bad[0]["total_ticks"] = 0
        bad[0]["prep_ticks"] = 0
        bad[0]["scan_ticks"] = 0
        self.write_capture(rows=bad)
        with self.assertRaisesRegex(ValueError, "invalid QPC timing"):
            ANALYZER.analyze(self.scan, self.windows, self.presents)

    def test_invalid_cycle_sample_is_excluded_without_rejecting_wall_timing(self):
        rows = [dict(r) for r in self.rows]
        rows[0]["total_cycles_valid"] = 0
        rows[0]["prep_cycles_valid"] = 0
        rows[0]["total_thread_cycles"] = 0
        rows[0]["prep_thread_cycles"] = 0
        rows[0]["scan_thread_cycles"] = 0
        self.write_capture(rows=rows)
        report = ANALYZER.analyze(self.scan, self.windows, self.presents)
        self.assertEqual(report["validation"]["invalid_cycle_rows"], 1)
        self.assertEqual(report["windows"][0]["thread_cycles"]["valid_scans"], 1)
        self.assertEqual(report["windows"][0]["thread_cycles"]["invalid_scans"], 1)

    def test_windows_and_present_clock_must_match(self):
        self.windows.write_text(json.dumps({"qpc_frequency": 999, "complete": True, "blocks": []}), encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "frequency"):
            ANALYZER.analyze(self.scan, self.windows, self.presents)
        self.windows.write_text(json.dumps({"qpc_frequency": 1_000_000, "complete": True, "blocks": [
            {"start_qpc": 100, "end_qpc": 200}, {"start_qpc": 190, "end_qpc": 300}]}), encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "overlap"):
            ANALYZER.analyze(self.scan, self.windows, self.presents)
        self.presents.write_text("# qpc_frequency=999\npresent_id,guest_frame,qpc\n1,0,1\n", encoding="utf-8")
        self.windows.write_text(json.dumps({"qpc_frequency": 1_000_000, "complete": True, "blocks": [
            {"start_qpc": 100, "end_qpc": 200}]}), encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "presents and taskset-scan QPC frequencies"):
            ANALYZER.analyze(self.scan, self.windows, self.presents)


if __name__ == "__main__":
    unittest.main()
