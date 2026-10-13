"""Validate filtered draw-callback timings; raw thread cycles are not CPU ms."""
import argparse
import csv
import json
import math
import re
from pathlib import Path

STAGES = ('geometry_preparation_ms', 'pipeline_constants_ms',
          'resource_resolution_ms', 'command_recording_ms')
FLOATS = ('wall_ms', *STAGES)
MASKS = ('texture_mask', 'vertex_texture_mask')


def validate_rows(rows):
    previous_id = 0
    frequency = None
    for row in rows:
        if any(not math.isfinite(row[key]) or row[key] < 0 for key in FLOATS):
            raise ValueError('Invalid elapsed time')
        if row['callback_id'] <= previous_id:
            raise ValueError('Nonmonotonic callback IDs')
        previous_id = row['callback_id']
        if row['qpc_frequency'] <= 0 or (frequency and frequency != row['qpc_frequency']):
            raise ValueError('Invalid or changing QPC frequency')
        frequency = row['qpc_frequency']
        if row['qpc_end'] < row['qpc_begin']:
            raise ValueError('Reversed callback interval')
        wall = (row['qpc_end'] - row['qpc_begin']) * 1000 / frequency
        if abs(row['wall_ms'] - wall) > 0.000001:
            raise ValueError('QPC and reported wall time disagree')
        if abs(sum(row[key] for key in STAGES) - row['wall_ms']) > 0.000003:
            raise ValueError('Stages do not partition callback wall time')
        if row['thread_cycles_valid'] not in (0, 1):
            raise ValueError('Invalid cycle validity flag')
        if row['thread_cycles_valid']:
            cycles = row['thread_cycles_end'] - row['thread_cycles_begin']
            if cycles < 0 or cycles != row['thread_cycles_delta_not_ms']:
                raise ValueError('Invalid raw cycle delta')
        elif row['thread_cycles_delta_not_ms'] != 0:
            raise ValueError('Invalid cycle query has nonzero reported delta')
    return frequency


def analyze(csv_path, windows_path, log_path, capacity=256):
    if not isinstance(capacity, int) or isinstance(capacity, bool) or capacity <= 0:
        raise ValueError('Ring capacity must be a positive integer')
    rows = []
    with Path(csv_path).open(newline='') as stream:
        for raw in csv.DictReader(stream):
            if None in raw or any(value is None for value in raw.values()):
                raise ValueError('Nonrectangular CSV')
            rows.append({key: float(value) if key in FLOATS else
                         int(value, 16) if key in MASKS else int(value)
                         for key, value in raw.items()})
    frequency = validate_rows(rows)
    matches = re.findall(r'\[slow-draw\] saved=(\d+) overwritten=(\d+) callbacks=(\d+) threshold_us=(\d+)',
                         Path(log_path).read_text(encoding='utf-8', errors='replace'))
    if len(matches) != 1:
        raise ValueError('Expected exactly one recorder summary')
    saved, overwritten, callbacks, threshold = map(int, matches[0])
    if saved != len(rows) or saved > capacity or saved + overwritten > callbacks:
        raise ValueError('Recorder counts disagree')
    if overwritten and saved != capacity:
        raise ValueError('Overwritten ring is not full')
    if rows and max(row['callback_id'] for row in rows) > callbacks:
        raise ValueError('Callback ID exceeds final count')
    if any(row['threshold_us'] != threshold or row['wall_ms'] + 0.000001 < threshold / 1000
           for row in rows):
        raise ValueError('Threshold mismatch')
    windows = json.loads(Path(windows_path).read_text(encoding='utf-8-sig'))
    if not windows['complete'] or (frequency and windows['qpc_frequency'] != frequency):
        raise ValueError('Incomplete windows or clock mismatch')
    blocks = []
    for index, block in enumerate(windows['blocks']):
        lo, hi = block['start_qpc'], block['end_qpc']
        if hi <= lo:
            raise ValueError('Empty or reversed measurement window')
        selected = [row for row in rows if lo <= row['qpc_begin'] <= row['qpc_end'] <= hi]
        edges = [row for row in rows if row['qpc_begin'] < hi and row['qpc_end'] > lo
                 and not lo <= row['qpc_begin'] <= row['qpc_end'] <= hi]
        blocks.append(dict(index=index, flags=block['flags'], retained_full_callbacks=len(selected),
                           retained_edge_callbacks=len(edges),
                           longest_retained_callbacks=sorted(selected, key=lambda r:r['wall_ms'], reverse=True)[:12]))
    return dict(note='Filtered retained callbacks only, not complete-frame costs or an isolation audit. '
                     'Stages partition each callback but can contain existing nested timers. '
                     'Raw thread cycles are not CPU milliseconds; absence from this ring does not rule out a stall.',
                saved=saved, overwritten=overwritten, callbacks=callbacks, capacity=capacity,
                threshold_us=threshold,
                qpc_frequency=frequency, first_retained_begin_qpc=min((r['qpc_begin'] for r in rows), default=None),
                blocks=blocks)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('csv', type=Path)
    parser.add_argument('--windows', required=True, type=Path)
    parser.add_argument('--log', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--capacity', type=int, default=256,
                        help='recorder ring capacity (default: historical profile ring 256)')
    args = parser.parse_args()
    result = analyze(args.csv, args.windows, args.log, capacity=args.capacity)
    args.output.write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
