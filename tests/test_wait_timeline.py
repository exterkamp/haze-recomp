"""Prevent numeric syscall/HLE confusion in wait-profile interpretation."""
import csv
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from summarize_waits import load_names, summarize


class WaitTimelineTests(unittest.TestCase):
    def test_syscall_numbers_come_from_runtime_table(self):
        names = load_names(Path(__file__).resolve().parents[1])
        self.assertEqual(names['syscall']['0000006B'], 'sys_cond_wait')
        self.assertEqual(names['syscall']['0000005C'], 'sys_semaphore_wait')
        self.assertEqual(names['syscall']['0000008D'], 'sys_timer_usleep')

    def test_kinds_are_separate_and_nested_wall_time_is_unioned(self):
        scratch = Path(__file__).resolve().parents[1] / 'local'
        scratch.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='wait-timeline-', dir=scratch) as directory:
            root = Path(directory)
            renderer = root / 'renderer.csv'
            renderer.write_text('frame,qpc\n1,100\n2,1100\n')
            fields = ['begin_qpc', 'end_qpc', 'frequency', 'host_tid',
                      'guest_tid', 'kind', 'id', 'lr', 'arg0']
            with (root / 'wait-1.csv').open('w', newline='') as stream:
                writer = csv.DictWriter(stream, fieldnames=fields)
                writer.writeheader()
                for kind, begin, end in [('hle', 50, 900), ('syscall', 200, 800)]:
                    writer.writerow(dict(begin_qpc=begin, end_qpc=end, frequency=1000,
                        host_tid=1, guest_tid=1, kind=kind, id='0000006B', lr='00896C10', arg0='7'))
            result = summarize(root, renderer, 1, 2)
            groups = {row['kind']: row for row in result['groups']}
            self.assertEqual(groups['syscall']['name'], 'sys_cond_wait')
            self.assertNotEqual(groups['hle']['name'], 'sys_cond_wait')
            self.assertEqual(groups['hle']['wall_ms'], 800)
            self.assertEqual(groups['syscall']['wall_ms'], 600)
            self.assertEqual(result['threads'][0]['service_union_ms'], 800)


if __name__ == '__main__':
    unittest.main()
