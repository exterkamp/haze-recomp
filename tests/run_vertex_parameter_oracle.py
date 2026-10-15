"""Compare native constant batches with the user's private original function.

Run in a Visual Studio developer environment. No game source is committed.
This tests sufficient-capacity uploads, not refill, guest ownership or drawing.
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
    commands = [[a.compiler, '/nologo', '/std:c++17', '/EHsc', '/O2',
        '/I' + str(ROOT / 'port'), '/I' + str(out),
        str(ROOT / 'tests/test_vertex_parameter_batch.cpp'), '/Fe' + str(out / 'oracle.exe')],
        [str(out / 'oracle.exe')]]
    for label, command in zip(['build', 'oracle'], commands):
        result = subprocess.run(command, cwd=out, env=os.environ.copy(), text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        (out / (label + '.log')).write_text(result.stdout)
        if result.returncode: raise RuntimeError(result.stdout)
    result = dict(passed=True, original_function_sha256=EXPECTED, commands=commands,
                  native_header_sha256=hashlib.sha256((ROOT / 'port/haze_vertex_parameter_batch.h').read_bytes()).hexdigest(),
                  result=(out / 'oracle.log').read_text(),
                  scope='Constant bytes, unchanged lanes, final method window/load and descriptor flag result; no refill, live ownership or GPU parity claim.')
    (out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(result['result'])

if __name__ == '__main__': main()
