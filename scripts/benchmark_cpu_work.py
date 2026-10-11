"""Alternate measured renderer CPU variants within one verified native scene.

Requires a profiling build, RSX_CPU_WORK_FILE and RSX_PROFILE_CSV. Do not move
the camera or request readbacks during the blocks. Flags: 1 bulk references,
2 direct dense remap, 4 XXH3 texture checks. Bit 8 also selects the experimental
sleep backend if PS3_USLEEP_MODE_FILE points to this same control file.
Bit 16 selects private SPU history if SPU_DEPTH_RING_FILE points to it too;
without that diagnostic control, the corrected private history is always used.
Zero selects the previous paths.
The runner stays muted. Compare individual stage costs as well as FPS: guest
work and NPC activity can drift even when the camera remains stationary.
"""
import argparse
import csv
import io
import json
from pathlib import Path
import time


def tail_rows(path):
    with path.open('rb') as stream:
        header = stream.readline().decode('utf-8').strip().split(',')
        stream.seek(0, 2)
        end = stream.tell()
        start = max(0, end - 256 * 1024)
        stream.seek(start)
        if start:
            stream.readline()  # Skip the partial first row.
        else:
            stream.readline()  # Skip the header.
        data = stream.read()
    data = data[:data.rfind(b'\n') + 1]
    rows = []
    for row in csv.DictReader(io.StringIO(data.decode('utf-8')), fieldnames=header):
        try:
            values = {k: float(v) for k, v in row.items()}
            if 'cpu_work' not in values:
                raise RuntimeError('This CSV lacks CPU variant markers; rebuild first.')
            rows.append(values)
        except (ValueError, TypeError):
            continue
    return rows


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('csv', type=Path)
    p.add_argument('control', type=Path)
    p.add_argument('--flags', type=int, nargs='+', default=[0,1,0,2,0,4,0,7,0,7])
    p.add_argument('--warmup', type=int, default=80)
    p.add_argument('--frames', type=int, default=180)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    if any(f < 0 or f > 31 for f in a.flags) or a.frames < 60 or a.warmup < 60:
        p.error('Flags must be 0..31; use at least 60 warmup and measured frames.')
    blocks = []
    try:
        for flags in a.flags:
            rows = tail_rows(a.csv)
            previous = int(rows[-1]['frame'])
            a.control.write_text(f'{flags}\n')
            first = None
            deadline = time.monotonic() + 180
            while time.monotonic() < deadline:
                rows = tail_rows(a.csv)
                matched = [r for r in rows if r['frame'] > previous
                           and r['cpu_work'] == flags]
                if matched and first is None:
                    first = int(matched[0]['frame']) + a.warmup
                if first is not None and matched and matched[-1]['frame'] >= first+a.frames-1:
                    selected = [r for r in matched if first <= r['frame'] < first+a.frames]
                    if len(selected) != a.frames:
                        raise RuntimeError('Missing gameplay frames inside benchmark block.')
                    if sum(r['draws'] for r in selected)/len(selected) < 500:
                        raise RuntimeError('Block does not match the verified gameplay workload.')
                    block = dict(flags=flags, first_frame=first, last_frame=first+a.frames-1,
                                 frames=a.frames, fps=1000/(sum(r['dt_ms'] for r in selected)/len(selected)))
                    blocks.append(block)
                    a.output.write_text(json.dumps(dict(blocks=blocks, note=__doc__),indent=2))
                    print(json.dumps(block), flush=True)
                    break
                time.sleep(.25)
            else:
                raise TimeoutError(f'Variant {flags} did not finish in 180 seconds')
    finally:
        a.control.write_text('0\n')


if __name__ == '__main__':
    main()
