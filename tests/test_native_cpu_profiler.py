"""Live optimized-call-chain oracle for the optional Windows stack sampler."""
import json
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
fixture = root / 'local' / 'profiler-stack-oracle' / 'HazeRecomp.exe'
child = subprocess.Popen([str(fixture), '8'], stdout=subprocess.PIPE, text=True)
try:
    tid = int(child.stdout.readline())
    data = json.loads(subprocess.check_output([
        sys.executable, str(root / 'scripts' / 'profile_native_cpu.py'),
        str(child.pid), '3', '--tid', str(tid), '--symbol-path', str(fixture.parent),
        '--stack-depth', '12', '--stack-every', '1'], text=True))
    assert len(data['threads']) == 1 and data['threads'][0]['tid'] == tid
    found = 0
    for stack in data['threads'][0]['stacks']:
        names = [frame.split('+0x')[0] for frame in stack['frames']]
        if names[:3] == ['oracle_leaf', 'oracle_middle', 'oracle_entry']:
            found += stack['samples']
    assert found >= 10, ('Too few correctly unwound optimized frames', data)
    assert data['suspension']['count'] > 0
    assert data['threads'][0]['cpu_ms'] > 100
    assert child.wait(timeout=15) == 0
    print(f'Native profiler oracle passed: {found} leaf/middle/entry stacks; '
          f'max suspension {data["suspension"]["max_ms"]:.3f} ms')
finally:
    if child.poll() is None:
        # This handle is the bounded test fixture launched above, never a game.
        child.terminate()
        child.wait(timeout=5)
