"""Summarize diagnostic Haze job spans without adding nested elapsed times."""
import argparse
import csv
import json
from collections import defaultdict
from pathlib import Path

KINDS = {1: 'submit', 2: 'dispatch_including_trampolines', 3: 'transform_kernel',
         4: 'completion_wait', 5: 'transform_handler'}


def union_ticks(intervals):
    total = 0
    end = None
    for lo, hi in sorted(intervals):
        total += max(0, hi - max(lo, end if end is not None else lo))
        end = max(hi, end if end is not None else hi)
    return total


def read_trace(path):
    events, summary = [], None
    with Path(path).open(newline='') as stream:
        for raw in csv.DictReader(stream):
            if None in raw or any(v is None for v in raw.values()):
                raise ValueError('Nonrectangular trace CSV')
            kind = raw.pop('record_type')
            row = {k: int(v) if v else 0 for k, v in raw.items()}
            if kind == 'summary':
                if summary is not None:
                    raise ValueError('Repeated summary')
                summary = row
            elif kind == 'event':
                if summary is not None or row['end_qpc'] < row['begin_qpc'] or row['kind'] not in KINDS:
                    raise ValueError('Invalid span')
                if events and row['ticket'] <= events[-1]['ticket']:
                    raise ValueError('Nonmonotonic completion tickets')
                events.append(row)
            else:
                raise ValueError('Unknown trace record')
    if not events or summary is None or summary['qpc_frequency'] <= 0:
        raise ValueError('Missing events, summary, or clock frequency')
    by_id = {row['span_id']: row for row in events}
    if len(by_id) != len(events):
        raise ValueError('Repeated span ID')
    for row in events:
        parent = by_id.get(row['parent_id'])
        if bool(row['parent_missing_in_dump']) != bool(row['parent_id'] and parent is None):
            raise ValueError('Parent retention annotation mismatch')
        if parent and (row['host_tid'] != parent['host_tid'] or row['depth'] != parent['depth'] + 1
                       or not parent['begin_qpc'] <= row['begin_qpc'] <= row['end_qpc'] <= parent['end_qpc']):
            raise ValueError('Invalid nested span')
    return events, summary


def summarize_window(events, summary, block, frames):
    lo, hi = block['start_qpc'], block['end_qpc']
    if hi <= lo or frames <= 0:
        raise ValueError('Invalid measurement window')
    scale = 1000 / summary['qpc_frequency']
    selected = [r for r in events if r['begin_qpc'] < hi and r['end_qpc'] > lo]
    groups = {}
    for kind, name in KINDS.items():
        rows = [r for r in selected if r['kind'] == kind]
        by_thread = defaultdict(list)
        for row in rows:
            by_thread[row['host_tid']].append((max(lo, row['begin_qpc']), min(hi, row['end_qpc'])))
        union_ms = sum(union_ticks(v) for v in by_thread.values()) * scale
        groups[name] = {
            'overlapping_spans': len(rows),
            'fully_contained_spans': sum(lo <= r['begin_qpc'] <= r['end_qpc'] <= hi for r in rows),
            'clipped_thread_union_ms': union_ms,
            'clipped_thread_union_ms_per_frame': union_ms / frames,
            'max_full_span_ms': max(((r['end_qpc'] - r['begin_qpc']) * scale for r in rows
                                     if lo <= r['begin_qpc'] <= r['end_qpc'] <= hi), default=0),
        }
    dispatches = defaultdict(list)
    for row in selected:
        if row['kind'] == 2:
            dispatches[(row['dispatch_code'], row['dispatch_toc'])].append(row)
    targets = []
    for (code, toc), rows in dispatches.items():
        targets.append({'code': f'0x{code:08X}', 'toc': f'0x{toc:08X}', 'spans': len(rows),
                        'inclusive_clipped_ms': sum(min(hi, r['end_qpc']) - max(lo, r['begin_qpc'])
                                                    for r in rows) * scale})
    # Completed spans only. Unfinished work and discarded intervals prevent a completeness claim.
    cutoff = summary['max_discarded_end_qpc']
    complete = cutoff <= lo and summary['incomplete_scopes'] == 0
    submissions = {(r['slot'], r['generation']) for r in events
                   if r['kind'] == 1 and r['slot'] < 512 and r['generation']}
    waits = [r for r in selected if r['kind'] == 4]
    return {'start_qpc': lo, 'end_qpc': hi, 'frames': frames,
            'completed_span_coverage': complete,
            'discarded_end_qpc_max': cutoff, 'incomplete_scopes_at_dump': summary['incomplete_scopes'],
            'groups': groups,
            'dispatch_targets': sorted(targets, key=lambda r: r['inclusive_clipped_ms'], reverse=True),
            'waits_with_retained_submit_generation': sum((r['slot'], r['generation']) in submissions for r in waits)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('--windows', required=True, type=Path)
    parser.add_argument('--pump', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    from summarize_pump import read_pump
    events, summary = read_trace(args.trace)
    windows = json.loads(args.windows.read_text(encoding='utf-8-sig'))
    pump = read_pump(args.pump)
    if summary['qpc_frequency'] != windows['qpc_frequency'] or summary['qpc_frequency'] != pump[0]['qpc_frequency']:
        raise ValueError('Trace/window/pump clock mismatch')
    blocks = []
    for block in windows['blocks']:
        lo, hi = block['start_qpc'], block['end_qpc']
        if pump[0]['begin_qpc'] > lo or pump[-1]['end_qpc'] < hi:
            raise ValueError('Pump does not cover the window')
        frames = sum(lo <= r['begin_qpc'] < r['end_qpc'] <= hi for r in pump)
        blocks.append(summarize_window(events, summary, block, frames))
    result = {'source': str(args.trace), 'retained_spans': len(events), 'summary': summary,
              'note': 'Diagnostic wall time, not CPU time. Groups overlap: never add submit, dispatch, handler, and kernel. Thread unions remove nesting within each kind, but separate threads can overlap. Dispatch includes trampoline completion. Generation matching is an observed native snapshot, not proof of a guest dependency. Incomplete coverage gives lower bounds only.',
              'blocks': blocks}
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
