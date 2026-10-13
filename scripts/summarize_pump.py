"""Summarize disjoint render-pump intervals; renderer stage scopes stay nested.

Use only diagnostic runs. Thread CPU accounting may be quantized by Windows;
its aggregate is useful, but a single row is not a scheduling trace.
"""
import argparse
import bisect
import csv
import json
import math
from pathlib import Path


def percentile(values, p):
    values = sorted(values)
    i = (len(values) - 1) * p
    lo = int(i)
    return values[lo] + (values[min(lo + 1, len(values) - 1)] - values[lo]) * (i - lo)


def read_pump(path):
    with Path(path).open(newline='') as f:
        rows = list(csv.DictReader(f))
    if not rows:
        raise ValueError('No pump intervals')
    previous = None
    fields = ('dt_ms', 'wait_ms', 'fifo_ms', 'present_ms', 'tick_ms', 'other_ms')
    for row in rows:
        for k in ('qpc_frequency', 'frame', 'flips', 'begin_qpc', 'end_qpc',
                  'cpu_valid', 'waits', 'queued_waits', 'drains'):
            row[k] = int(row[k])
        for k in (*fields, 'thread_cpu_ms'):
            row[k] = float(row[k])
        if not all(math.isfinite(row[k]) for k in (*fields, 'thread_cpu_ms')):
            raise ValueError('Nonfinite pump accounting')
        if row['qpc_frequency'] <= 0 or row['end_qpc'] <= row['begin_qpc']:
            raise ValueError('Invalid pump timestamp')
        if previous and (row['begin_qpc'] != previous['end_qpc'] or
                         row['frame'] != previous['frame'] + 1 or
                         row['qpc_frequency'] != previous['qpc_frequency']):
            raise ValueError('Nonconsecutive pump intervals')
        if row['queued_waits'] > row['waits'] or any(row[k] < 0 for k in ('waits', 'queued_waits', 'drains')):
            raise ValueError('Invalid queued-wait counts')
        if row['cpu_valid'] not in (0, 1) or row['thread_cpu_ms'] < 0:
            raise ValueError('Invalid thread CPU accounting')
        expected = (row['end_qpc']-row['begin_qpc'])*1000/row['qpc_frequency']
        if abs(expected-row['dt_ms']) > 0.00001 or any(row[k] < -0.00001 for k in fields):
            raise ValueError('Invalid interval accounting')
        if abs(sum(row[k] for k in fields[1:])-row['dt_ms']) > 0.00001:
            raise ValueError('Scope totals do not match interval')
        previous = row
    return rows


def summarize(path, start=None, end=None, renderer=None):
    rows = read_pump(path)
    fields = ('dt_ms', 'wait_ms', 'fifo_ms', 'present_ms', 'tick_ms', 'other_ms')
    if start is not None and (end is None or end <= start):
        raise ValueError('Invalid requested QPC window')
    if start is not None and (rows[0]['begin_qpc'] > start or rows[-1]['end_qpc'] < end):
        raise ValueError('Pump ring does not cover requested window')
    selected = [r for r in rows if start is None or start <= r['begin_qpc'] < r['end_qpc'] <= end]
    if not selected:
        raise ValueError('No full intervals in window')
    render_fields = ('draw_cpu_ms', 'vertex_pack_ms', 'fence_wait_ms', 'gpu_ms',
                     'vp_prepare_ms', 'fp_prepare_ms', 'texture_hash_ms', 'texture_decode_ms',
                     'pso_lookup_ms', 'frame_limit_ms', 'blit_cpu_ms')
    if renderer:
        with Path(renderer).open(newline='') as f:
            render_rows = list(csv.DictReader(f))
        qpcs = [int(r['qpc']) for r in render_rows]
        if qpcs != sorted(set(qpcs)):
            raise ValueError('Nonmonotonic renderer timestamps')
        for row in selected:
            lo = bisect.bisect_right(qpcs, row['begin_qpc'])
            hi = bisect.bisect_right(qpcs, row['end_qpc'])
            if hi-lo != 1:
                raise ValueError(f'Expected one renderer present in pump frame {row["frame"]}, got {hi-lo}')
            rr = render_rows[lo]
            if int(rr['qpc_frequency']) != row['qpc_frequency']:
                raise ValueError('Renderer QPC frequency mismatch')
            row['renderer'] = {k: float(rr[k]) for k in render_fields}

    def group(items):
        if not items:
            return {'frames': 0}
        result = {'frames': len(items), 'means_ms': {
            k: sum(r[k] for r in items)/len(items) for k in fields},
            'frame_ms': {k: percentile([r['dt_ms'] for r in items], p)
                         for k, p in (('p50', .5), ('p95', .95), ('p99', .99), ('max', 1))}}
        cpu = [r for r in items if r['cpu_valid']]
        result['thread_cpu_valid_frames'] = len(cpu)
        result['thread_cpu_mean_ms'] = sum(r['thread_cpu_ms'] for r in cpu)/len(cpu) if cpu else None
        result['mean_waits'] = sum(r['waits'] for r in items)/len(items)
        result['mean_queued_waits'] = sum(r['queued_waits'] for r in items)/len(items)
        result['mean_drains'] = sum(r['drains'] for r in items)/len(items)
        if renderer:
            result['nested_renderer_means_ms'] = {
                k: sum(r['renderer'][k] for r in items)/len(items) for k in render_fields}
        return result

    return {'source': str(path), 'note': 'Diagnostic intervals at guest-present return. Wait/FIFO/present/tick/other are disjoint wall scopes; nested renderer scopes and GPU time must not be added to them. Thread CPU is approximate Windows accounting. Queued waits indicate entry state, not whether a wait slept.',
            'all': group(selected), 'under_33_333_ms': group([r for r in selected if r['dt_ms'] < 33.333]),
            'over_50_ms': group([r for r in selected if r['dt_ms'] > 50]),
            'over_100_ms': group([r for r in selected if r['dt_ms'] > 100]),
            'slowest': sorted(selected, key=lambda r: r['dt_ms'], reverse=True)[:10]}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('csv')
    p.add_argument('--windows', help='Present-mode record manifest; summarize each full window')
    p.add_argument('--renderer')
    p.add_argument('--output')
    args = p.parse_args()
    if args.windows:
        windows = json.loads(Path(args.windows).read_text())
        if int(windows['qpc_frequency']) != read_pump(args.csv)[0]['qpc_frequency']:
            raise ValueError('Present window QPC frequency mismatch')
        result = {'blocks': [dict(flags=w['flags'], **summarize(args.csv, w['start_qpc'], w['end_qpc'], args.renderer)) for w in windows['blocks']]}
    else:
        result = summarize(args.csv, renderer=args.renderer)
    text = json.dumps(result, indent=2) + '\n'
    if args.output:
        Path(args.output).write_text(text)
    print(text)


if __name__ == '__main__':
    main()
