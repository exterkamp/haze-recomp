"""Measure renderer variants in an ordinary, nonprofiling native build.

Record timed blocks while a visually verified gameplay scene stays still.
The selected mode's file must point to CONTROL and RSX_PRESENT_CSV must be set before
launch. Close the owned game normally afterward, then summarize its fixed-ring
CSV against the recorded QPC windows. No screenshots or profiling may run
during these blocks. Menus/loading do not establish gameplay performance.
"""
import argparse
import csv
import ctypes as c
import json
from pathlib import Path
import statistics
import time

from benchmark_cpu_work import process_clock
from summarize_presents import percentile


def record(args):
    from ctypes import wintypes as w
    kernel = c.WinDLL('kernel32', use_last_error=True)
    kernel.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
    kernel.OpenProcess.restype = w.HANDLE
    kernel.QueryFullProcessImageNameW.argtypes = [w.HANDLE, w.DWORD, w.LPWSTR, c.POINTER(w.DWORD)]
    kernel.CloseHandle.argtypes = [w.HANDLE]
    handle = kernel.OpenProcess(0x1000, False, args.pid)
    if not handle:
        raise c.WinError(c.get_last_error())
    try:
        path = c.create_unicode_buffer(32768)
        size = w.DWORD(len(path))
        if not kernel.QueryFullProcessImageNameW(handle, 0, path, c.byref(size)):
            raise c.WinError(c.get_last_error())
        if Path(path.value).resolve() != args.exe.resolve():
            raise RuntimeError('PID does not match the explicitly selected benchmark executable')
    finally:
        kernel.CloseHandle(handle)
    frequency = c.c_longlong()
    if not kernel.QueryPerformanceFrequency(c.byref(frequency)):
        raise c.WinError(c.get_last_error())

    def qpc():
        value = c.c_longlong()
        if not kernel.QueryPerformanceCounter(c.byref(value)):
            raise c.WinError(c.get_last_error())
        return value.value

    cpu, close = process_clock(args.pid)
    result = dict(pid=args.pid, exe=str(args.exe.resolve()),
                  qpc_frequency=frequency.value, warmup_s=args.warmup,
                  measure_s=args.seconds, mode_column=args.mode_column,
                  blocks=[], complete=False, note=__doc__)

    def wait(seconds):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            cpu()  # Fail promptly if the explicitly owned run exits.
            time.sleep(min(.25, max(0, deadline - time.monotonic())))

    try:
        for flags in args.flags:
            args.control.write_text(f'{flags}\n')
            wait(args.warmup)
            start = qpc()
            cpu_start = cpu()
            wait(args.seconds)
            cpu_ms = cpu() - cpu_start
            end = qpc()
            block = dict(flags=flags, start_qpc=start, end_qpc=end,
                         process_cpu_ms=cpu_ms,
                         mean_cores=cpu_ms * frequency.value / (end - start) / 1000)
            result['blocks'].append(block)
            args.output.write_text(json.dumps(result, indent=2) + '\n')
            print(json.dumps(block), flush=True)
        result['complete'] = True
    finally:
        close()
        args.control.write_text(f'{args.flags[0]}\n')
        args.output.write_text(json.dumps(result, indent=2) + '\n')


def report(args):
    windows = json.loads(args.windows.read_text())
    if not windows['complete']:
        raise ValueError('Timed block recording is incomplete')
    with args.csv.open(newline='') as stream:
        header = stream.readline().strip()
        if header != f"# qpc_frequency={windows['qpc_frequency']}":
            raise ValueError('Present CSV QPC frequency differs from the recording')
        rows = [{key: int(value) for key, value in row.items()}
                for row in csv.DictReader(stream)]
    summaries = []
    mode_column = windows.get('mode_column', 'cpu_work')
    if mode_column not in ('cpu_work', 'shadow_compare', 'depth_export', 'window_position'):
        raise ValueError('Unknown recorded mode column')
    for block in windows['blocks']:
        selected = [(prev, cur) for prev, cur in zip(rows, rows[1:])
                    if prev['qpc'] >= block['start_qpc'] and cur['qpc'] <= block['end_qpc']]
        if len(selected) < args.min_frames:
            raise ValueError('Too few complete present intervals in a recorded block')
        for prev, cur in selected:
            if cur['present_id'] != prev['present_id'] + 1:
                raise ValueError('Missing present inside a recorded block')
            if prev[mode_column] != block['flags'] or cur[mode_column] != block['flags']:
                raise ValueError('Mode did not settle before the measured block; increase warmup')
            if cur['qpc'] <= prev['qpc']:
                raise ValueError('Nonpositive present interval')
        intervals = [(cur['qpc'] - prev['qpc']) * 1000 / windows['qpc_frequency']
                     for prev, cur in selected]
        summaries.append(dict(block, frames=len(intervals),
            first_frame=selected[0][1]['guest_frame'], last_frame=selected[-1][1]['guest_frame'],
            fps=1000 / statistics.mean(intervals),
            frame_ms=dict(mean=statistics.mean(intervals), min=min(intervals),
                          p95=percentile(intervals, .95), p99=percentile(intervals, .99), max=max(intervals)),
            frames_over_33_333_ms=sum(value > 1000 / 30 for value in intervals),
            frames_over_50_ms=sum(value > 50 for value in intervals)))
    result = dict(source=str(args.csv), windows=str(args.windows), blocks=summaries, note=__doc__)
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    actions = parser.add_subparsers(dest='action', required=True)
    runner = actions.add_parser('record')
    runner.add_argument('control', type=Path)
    runner.add_argument('--pid', type=int, required=True)
    runner.add_argument('--exe', type=Path, required=True)
    runner.add_argument('--flags', nargs='+', type=int, default=[259,771,259,771,259,771])
    runner.add_argument('--mode-column', choices=['cpu_work','shadow_compare','depth_export','window_position'], default='cpu_work',
                        help='CSV state to validate; set the matching renderer control-file environment variable')
    runner.add_argument('--warmup', type=float, default=5)
    runner.add_argument('--seconds', type=float, default=12)
    runner.add_argument('--output', type=Path, required=True)
    reader = actions.add_parser('report')
    reader.add_argument('csv', type=Path)
    reader.add_argument('windows', type=Path)
    reader.add_argument('--min-frames', type=int, default=180)
    reader.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.action == 'record':
        if args.warmup < 5 or args.seconds < 10 or any(not 0 <= f <= 1023 for f in args.flags):
            parser.error('Use at least 5 warmup seconds, 10 measured seconds, and flags 0..1023')
        if args.mode_column != 'cpu_work' and any(f not in (0,1) for f in args.flags):
            parser.error('Shadow/depth export flags must be 0 or 1')
        record(args)
    else:
        if args.min_frames < 60:
            parser.error('Use at least 60 complete intervals per block')
        report(args)
