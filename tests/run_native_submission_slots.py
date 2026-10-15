"""Real D3D12 WARP ownership fixture; requires a VS developer environment.

Uses the prototype header directly and, when available, the SDK's unchanged
GPU query helpers. Does not launch the game or require game data. Arena sizes
are scaled for the fixture; this does not establish live renderer coverage.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--compiler', default=os.environ.get('CLANG_CL') or shutil.which('clang-cl'))
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    if not args.compiler:
        parser.error('clang-cl is required; run from a VS developer environment')
    root = Path(__file__).resolve().parents[1]
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=False)
    video = args.sdk.resolve() / 'libs/video'
    source = video / 'rsx_live_draw.c'
    text = source.read_text(encoding='utf-8')
    begin, end = '/* BEGIN RSX GPU PROFILE HELPERS */', '/* END RSX GPU PROFILE HELPERS */'
    profiling = begin in text and end in text
    inputs = [root / 'port/haze_native_submission_slots.h',
              root / 'tests/test_native_submission_slots.c', video / 'rsx_postprocess.h']
    if profiling:
        assert text.count(begin) == text.count(end) == 1
        helpers = output / 'gpu_profile_actual.inc'
        helpers.write_text(text[text.index(begin):text.index(end)], encoding='utf-8')
        inputs.append(helpers)
    executable = output / 'test_native_submission_slots.exe'
    command = [args.compiler, '/nologo', '/TC', '/W4', '/WX', '/O2']
    if profiling:
        command.append('/DYZ_PERF_PROFILE=1')
    command += ['/I' + str(root / 'port'), '/I' + str(video), '/I' + str(output),
                str(inputs[1]), '/Fo' + str(output / 'test.obj'), '/Fe' + str(executable),
                '/link', 'd3d12.lib', 'dxgi.lib', 'dxguid.lib', 'd3dcompiler.lib']
    built = subprocess.run(command, cwd=output, capture_output=True, text=True, timeout=90)
    (output / 'compile.log').write_text(built.stdout + built.stderr, encoding='utf-8')
    report = dict(compile_exit=built.returncode, profiling_helpers=profiling, command=command,
                  source_sha256={str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs},
                  scope='WARP/debug-layer ownership fixture, scaled arenas. Not gameplay or FPS evidence.')
    if built.returncode == 0:
        ran = subprocess.run([str(executable)], cwd=output, capture_output=True, text=True, timeout=60)
        (output / 'run.log').write_text(ran.stdout + ran.stderr, encoding='utf-8')
        report['run_exit'] = ran.returncode
        print(ran.stdout, end='')
    (output / 'result.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({k: v for k, v in report.items() if k.endswith('_exit') or k == 'profiling_helpers'}))
    return report.get('run_exit', report['compile_exit'])


if __name__ == '__main__':
    raise SystemExit(main())
