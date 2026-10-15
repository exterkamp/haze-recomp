"""Compare native constant batches with the user's private original function.

Run in a Visual Studio developer environment. No game source is committed.
With --sdk, also compare both actual SDK state handlers and mixed legacy writes.
This tests sufficient-capacity uploads and owned packet bytes, not live FIFO
publication/retirement, guest synchronization, refill or drawing.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

EXPECTED = '8f29ba4a6a92535c0fda9fd26fd4a4e34f639682f42d12d95b8c040b329479fd'
ROOT = Path(__file__).resolve().parents[1]

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source', type=Path, required=True)
    p.add_argument('--compiler', required=True)
    p.add_argument('--output-dir', type=Path, required=True)
    p.add_argument('--sdk', type=Path, help='SDK checkout containing libs/video')
    a = p.parse_args()
    try: source = a.source.read_text(encoding='utf-8')
    except UnicodeDecodeError: source = a.source.read_text(encoding='cp1252')
    signature = 'void func_0072E398(ppu_context* ctx) {'
    assert source.count(signature) == 1
    start = source.index(signature)
    end = source.find('\nvoid func_', start + len(signature))
    body = source[start:end if end >= 0 else None].strip()
    assert hashlib.sha256(body.encode()).hexdigest() == EXPECTED, 'Re-audit changed source'
    out = a.output_dir.resolve()
    out.mkdir(parents=True, exist_ok=False)
    (out / 'vertex_parameter_original.inc').write_text(body, encoding='utf-8')
    commands = []
    sdk_inputs = {}
    extra = []
    if a.sdk:
        sdk = a.sdk.resolve()
        video = sdk / 'libs/video'
        includes = ['/I' + str(sdk / 'include'), '/I' + str(video)]
        for name in ['rsx_dispatch', 'rsx_commands']:
            source_file = video / (name + '.c')
            obj = out / (name + '.obj')
            commands.append([a.compiler, '/nologo', '/TC', '/std:c11', '/O2',
                '/D_CRT_SECURE_NO_WARNINGS', *includes, '/c', str(source_file), '/Fo' + str(obj)])
            extra.append(str(obj))
            for suffix in ['.c', '.h']:
                f = video / (name + suffix)
                sdk_inputs[str(f)] = hashlib.sha256(f.read_bytes()).hexdigest()
        extra += includes + ['/DHAZE_TEST_VERTEX_PARAMETER_SDK=1']
    commands += [[a.compiler, '/nologo', '/std:c++17', '/EHsc', '/O2',
        '/I' + str(ROOT / 'port'), '/I' + str(out),
        str(ROOT / 'tests/test_vertex_parameter_batch.cpp'), *extra, '/Fe' + str(out / 'oracle.exe')],
        [str(out / 'oracle.exe')]]
    for i, command in enumerate(commands):
        label = 'oracle' if i == len(commands) - 1 else f'build-{i}'
        result = subprocess.run(command, cwd=out, env=os.environ.copy(), text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        (out / (label + '.log')).write_text(result.stdout)
        if result.returncode: raise RuntimeError(result.stdout)
    result = dict(passed=True, original_function_sha256=EXPECTED, commands=commands,
                  source_hashes={str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in [
                      ROOT / 'port/haze_vertex_parameter_batch.h',
                      ROOT / 'port/haze_vertex_parameter_packet.h',
                      ROOT / 'port/haze_vertex_parameter_consumer.h',
                      ROOT / 'tests/test_vertex_parameter_batch.cpp']},
                  sdk_inputs=sdk_inputs,
                  result=(out / 'oracle.log').read_text(),
                  scope='Constant bytes, unchanged lanes, final method window/load, descriptor flag result, and owned packet bytes. With --sdk: both full state mirrors, dirty ranges, coverage counters, and legacy continuation. No refill, live publication/retirement or GPU parity claim.')
    (out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(result['result'])

if __name__ == '__main__': main()
