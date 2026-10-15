import csv
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from scripts.analyze_completion_queue import FLAG_BITS, ROW_FIELDS, analyze


class CompletionQueueAnalysisTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.csv = self.directory / "queue.csv"
        self.meta_path = Path(str(self.csv)+".meta.json")
        self.windows_path = self.directory / "windows.json"

    @staticmethod
    def row(sequence=0, submit=195, publication=200, pop=210, **changes):
        row = dict(qpc_frequency=1000, sequence=sequence, queue_epoch=1,
                   publication_ordinal=sequence+1, submit_generation=1,
                   queue_ea=0x1000, slot=sequence, index=sequence,
                   flags=0, submit_qpc=submit, publication_qpc=publication,
                   pop_qpc=pop, producer_tid=12, consumer_tid=34)
        row.update(changes)
        return row

    def write(self, rows, meta=None, blocks=None, complete=True):
        metadata = dict(schema=1, qpc_frequency=1000, begin_qpc=100, end_qpc=1000,
                        recorded=len(rows), retained=len(rows), epochs=1,
                        publications=max((r["publication_ordinal"] for r in rows), default=0),
                        submits=len(rows), invalidations=0, unsupported_tail=0,
                        pending_overwritten=0, pending_at_stop=0, ambiguous_submits=0,
                        max_overwritten_pop_qpc=0, publication_is_locked_notification=True,
                        service_and_callback_phases="missing", flag_bits=FLAG_BITS)
        metadata.update(meta or {})
        with self.csv.open("w", newline="") as file:
            writer = csv.DictWriter(file, fieldnames=sorted(ROW_FIELDS))
            writer.writeheader()
            writer.writerows(rows)
        self.meta_path.write_text(json.dumps(metadata))
        if blocks is None:
            blocks = [dict(start_qpc=150, end_qpc=900, flags=13571, camera_token=910)]
        self.windows_path.write_text(json.dumps(dict(qpc_frequency=1000, blocks=blocks, complete=complete)))

    def report(self):
        return analyze(self.csv, windows_path=self.windows_path)

    def test_stats_and_per_block_no_duration_sums(self):
        rows = [self.row(i, 200+i*100-(i+1)*5, 200+i*100,
                         200+i*100+(i+1)*10) for i in range(4)]
        self.write(rows)
        report = self.report()
        self.assertEqual(report["status"], "complete")
        block = report["blocks"][0]
        self.assertEqual(block["flags"], 13571)
        self.assertEqual(block["camera_token"], 910)
        residency = block["publication_to_pop_ms"]
        self.assertEqual(residency["count"], 4)
        self.assertEqual(residency["mean_ms"], 25)
        self.assertEqual(residency["median_ms"], 25)
        self.assertAlmostEqual(residency["p95_ms"], 38.5)
        self.assertAlmostEqual(residency["p99_ms"], 39.7)
        self.assertEqual(residency["max_ms"], 40)
        self.assertEqual(block["submit_to_publication_ms"]["median_ms"], 12.5)
        self.assertNotIn("sum_ms", residency)
        self.assertNotIn("per_frame_ms", block)

    def test_half_open_windows_and_stage_crossings(self):
        self.write([self.row(0, 180, 190, 200), self.row(1, 280, 290, 310),
                    self.row(2, 300, 310, 320)], blocks=[
                        dict(start_qpc=150, end_qpc=300), dict(start_qpc=300, end_qpc=500)])
        first, second = self.report()["blocks"]
        self.assertEqual(first["popped_records"], 1)
        self.assertEqual(first["submit_to_publication_ms"]["count"], 2)
        self.assertEqual(first["publications_popping_at_or_after_end"], 1)
        self.assertEqual(second["popped_records"], 2)
        self.assertEqual(second["publication_to_pop_ms"]["count"], 1)
        self.assertEqual(second["publication_to_pop_crossing_start"], 1)
        self.assertEqual(second["submit_to_publication_ms"]["count"], 1)

    def test_overwritten_qpc_tie_requires_strictly_later_start(self):
        self.write([self.row(1, 395, 400, 401)],
                   meta=dict(recorded=2, max_overwritten_pop_qpc=400),
                   blocks=[dict(start_qpc=400, end_qpc=800), dict(start_qpc=401, end_qpc=800)])
        first, second = self.report()["blocks"]
        self.assertEqual(first["coverage"]["status"], "partial")
        self.assertIn("window_not_strictly_after_overwritten_pop_boundary", first["coverage"]["reasons"])
        self.assertEqual(second["coverage"]["status"], "complete")

    def test_unknown_generation_preserves_known_publication_residency(self):
        self.write([self.row(submit=0, submit_generation=0, flags=2)], meta=dict(submits=0))
        block = self.report()["blocks"][0]
        self.assertEqual(block["publication_to_pop_ms"]["count"], 1)
        self.assertEqual(block["submit_to_publication_ms"]["count"], 0)
        self.assertTrue(block["coverage"]["publication_to_pop_proven"])
        self.assertFalse(block["coverage"]["submit_to_publication_proven"])
        self.assertEqual(block["flags_by_popped_record"]["no_generation"], 1)

    def test_unknown_publication_and_payload_mismatch_excluded(self):
        self.write([self.row(submit=0, publication=0, publication_ordinal=0,
                             submit_generation=0, producer_tid=0, flags=3),
                    self.row(1, 295, 300, 310, flags=8)])
        block = self.report()["blocks"][0]
        self.assertEqual(block["flagged_popped_records"], 2)
        self.assertEqual(block["publication_to_pop_ms"]["count"], 0)
        self.assertEqual(block["coverage"]["status"], "partial")

    def test_bad_clock_cannot_be_assigned_to_a_safe_window(self):
        self.write([self.row(pop=0, flags=16)])
        report = self.report()
        self.assertEqual(report["status"], "partial")
        self.assertEqual(report["capture"]["flags_by_retained_record"]["bad_clock"], 1)
        self.assertIn("capture_has_unlocatable_bad_clock_rows", report["blocks"][0]["coverage"]["reasons"])

    def test_epochs_and_untimestamped_losses_are_explicit(self):
        self.write([self.row(), self.row(1, 395, 400, 410, queue_epoch=2)],
                   meta=dict(epochs=2, invalidations=1, unsupported_tail=1,
                             pending_overwritten=1, pending_at_stop=1, ambiguous_submits=1))
        block = self.report()["blocks"][0]
        self.assertEqual(block["coverage"]["status"], "partial")
        self.assertEqual(block["epochs_in_popped_records"], [1, 2])
        for reason in ("untimestamped_invalidations", "untimestamped_unsupported_tail",
                       "untimestamped_pending_overwritten", "schema1_pending_publication_bounds_unavailable",
                       "untimestamped_ambiguous_submits",
                       "multiple_queue_epochs_without_transition_timestamps"):
            self.assertIn(reason, block["coverage"]["reasons"])

    def test_legacy_pending_only_limits_publication_selected_cohort(self):
        self.write([self.row()], meta=dict(pending_at_stop=2, publications=3))
        block = self.report()["blocks"][0]
        self.assertTrue(block["coverage"]["publication_to_pop_proven"])
        self.assertFalse(block["coverage"]["submit_to_publication_proven"])
        self.assertEqual(block["pending_at_stop_publication_bounds"], "legacy_bounds_unavailable")

    def test_schema2_pending_after_window_is_proven_even_with_old_submit(self):
        for minimum in (900, 950):
            with self.subTest(minimum=minimum):
                self.write([self.row()], meta=dict(
                    schema=2, pending_at_stop=2, publications=3,
                    min_pending_publication_qpc=minimum, min_pending_submit_qpc=100,
                    pending_unknown_publication_qpc=0, pending_unknown_submit_qpc=1))
                block = self.report()["blocks"][0]
                self.assertEqual(block["coverage"]["status"], "complete")
                self.assertEqual(block["pending_at_stop_publication_bounds"], "at_or_after_end")

    def test_schema2_pending_inside_window_only_limits_producer_coverage(self):
        self.write([self.row()], meta=dict(
            schema=2, pending_at_stop=1, publications=2,
            min_pending_publication_qpc=899, min_pending_submit_qpc=890,
            pending_unknown_publication_qpc=0, pending_unknown_submit_qpc=0))
        coverage = self.report()["blocks"][0]["coverage"]
        self.assertTrue(coverage["publication_to_pop_proven"])
        self.assertFalse(coverage["submit_to_publication_proven"])
        self.assertEqual(coverage["publication_to_pop_reasons"], [])
        self.assertIn("pending_publications_may_overlap_window", coverage["submit_to_publication_reasons"])

    def test_schema2_unknown_pending_clocks_never_imply_safe_publication_bound(self):
        self.write([self.row()], meta=dict(
            schema=2, pending_at_stop=1, publications=2,
            min_pending_publication_qpc=0, min_pending_submit_qpc=950,
            pending_unknown_publication_qpc=1, pending_unknown_submit_qpc=0))
        block = self.report()["blocks"][0]
        self.assertTrue(block["coverage"]["publication_to_pop_proven"])
        self.assertFalse(block["coverage"]["submit_to_publication_proven"])
        self.assertEqual(block["pending_at_stop_publication_bounds"], "unknown_publication_clock")

    def test_schema2_inconsistent_pending_bounds_are_invalid(self):
        base = dict(schema=2, pending_at_stop=1, publications=2,
                    min_pending_publication_qpc=950, min_pending_submit_qpc=940,
                    pending_unknown_publication_qpc=0, pending_unknown_submit_qpc=0)
        for changed in (dict(min_pending_publication_qpc=0),
                        dict(pending_unknown_publication_qpc=2),
                        dict(min_pending_submit_qpc=960), dict(min_pending_publication_qpc=1100),
                        dict(pending_at_stop=0)):
            with self.subTest(changed=changed):
                self.write([self.row()], meta={**base, **changed})
                self.assertEqual(self.report()["status"], "invalid")

    def test_missing_or_short_capture_is_never_complete(self):
        self.write([self.row()], blocks=[dict(start_qpc=50, end_qpc=1100)])
        self.assertEqual(self.report()["status"], "partial")
        self.assertEqual(analyze(self.csv)["status"], "partial")
        self.write([self.row()], complete=False)
        self.assertEqual(self.report()["status"], "partial")
        self.write([self.row()], blocks=[])
        self.assertEqual(self.report()["status"], "partial")
        self.meta_path.unlink()
        self.assertEqual(self.report()["status"], "invalid")

    def test_zero_records_are_null_statistics_not_fake_latency(self):
        self.write([])
        report = self.report()
        self.assertEqual(report["status"], "complete")  # active empty queue, interval covered
        block = report["blocks"][0]
        self.assertEqual(block["sample_status"], "no_records")
        self.assertEqual(block["publication_to_pop_ms"]["count"], 0)
        self.assertIsNone(block["publication_to_pop_ms"]["mean_ms"])
        self.write([], meta=dict(epochs=0, qpc_frequency=0, begin_qpc=0))
        self.assertEqual(self.report()["status"], "partial")  # diagnostic never installed

    def test_invalid_metadata_rows_and_frequencies(self):
        for changes in (dict(qpc_frequency=-1), dict(retained=2), dict(end_qpc=50),
                        dict(schema=2), dict(epochs=0), dict(max_overwritten_pop_qpc=300),
                        dict(publications=2), dict(recorded=True)):
            with self.subTest(changes=changes):
                self.write([self.row()], meta=changes)
                self.assertEqual(self.report()["status"], "invalid")
        for changes in (dict(flags=32), dict(queue_epoch=0), dict(pop_qpc=199),
                        dict(submit_generation=0), dict(qpc_frequency=2000),
                        dict(sequence=1), dict(queue_ea=123)):
            with self.subTest(changes=changes):
                self.write([self.row(**changes)])
                self.assertEqual(self.report()["status"], "invalid")
        self.write([self.row()])
        windows = json.loads(self.windows_path.read_text())
        windows["qpc_frequency"] = 999
        self.windows_path.write_text(json.dumps(windows))
        self.assertEqual(self.report()["status"], "invalid")

    def test_duplicate_publication_or_generation_is_invalid(self):
        for changes in (dict(publication_ordinal=1), dict(slot=0)):
            self.write([self.row(), self.row(1, 295, 300, 310, **changes)])
            self.assertEqual(self.report()["status"], "invalid")

    def test_duplicate_json_keys_and_same_epoch_owner_change_are_invalid(self):
        self.write([self.row()])
        self.meta_path.write_text(self.meta_path.read_text().replace('"schema": 1', '"schema": 1, "schema": 1'))
        self.assertEqual(self.report()["status"], "invalid")
        self.write([self.row(), self.row(1, 295, 300, 310, queue_ea=0x2000)])
        self.assertEqual(self.report()["status"], "invalid")

    def test_cli_reports_invalid_with_nonzero_exit(self):
        self.write([self.row()], meta=dict(retained=99))
        output = self.directory / "analysis.json"
        script = Path(__file__).resolve().parents[1] / "scripts/analyze_completion_queue.py"
        result = subprocess.run([sys.executable, str(script), str(self.csv),
                                 "--windows", str(self.windows_path), "--output", str(output)],
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertEqual(json.loads(output.read_text())["status"], "invalid")


if __name__ == "__main__":
    unittest.main()
