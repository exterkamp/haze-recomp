"""Nested and parallel spans must not be misreported as additive frame costs."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from analyze_job_trace import summarize_window, union_ticks


class JobTraceAnalysis(unittest.TestCase):
    def test_union_nested_disjoint_touching(self):
        self.assertEqual(union_ticks([(0, 20), (5, 10), (20, 25), (30, 40)]), 35)
        self.assertEqual(union_ticks([]), 0)

    def test_clipping_nesting_and_parallelism(self):
        def row(kind, lo, hi, tid=1):
            return dict(kind=kind, begin_qpc=lo, end_qpc=hi, host_tid=tid,
                        slot=0, generation=1, dispatch_code=0x682900, dispatch_toc=0xE94EE0)
        events = [row(1, 0, 100), row(2, 10, 90), row(3, 20, 70), row(3, 30, 50), row(3, 40, 60, 2)]
        summary = dict(qpc_frequency=1000, max_discarded_end_qpc=0, incomplete_scopes=0)
        result = summarize_window(events, summary, dict(start_qpc=25, end_qpc=75), 2)
        self.assertTrue(result['completed_span_coverage'])
        self.assertEqual(result['groups']['submit']['clipped_thread_union_ms'], 50)
        self.assertEqual(result['groups']['transform_kernel']['clipped_thread_union_ms_per_frame'], 32.5)
        self.assertEqual(result['dispatch_targets'][0]['code'], '0x00682900')
        summary['max_discarded_end_qpc'] = 26
        self.assertFalse(summarize_window(events, summary, dict(start_qpc=25, end_qpc=75), 2)['completed_span_coverage'])
        summary.update(max_discarded_end_qpc=0, incomplete_scopes=1)
        self.assertFalse(summarize_window(events, summary, dict(start_qpc=25, end_qpc=75), 2)['completed_span_coverage'])


if __name__ == '__main__':
    unittest.main()
