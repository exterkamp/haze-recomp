"""Build a private original-batch replay oracle and validate captured fixtures.

Requires a VS x64 environment. Generated game operations and captures stay in
the supplied private output directory, never in source commits.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
from patch_job13_capture import validate


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--sdk',type=Path,required=True)
    p.add_argument('--source',type=Path,required=True)
    p.add_argument('--registration',type=Path,required=True)
    p.add_argument('--output-dir',type=Path,required=True)
    p.add_argument('--compiler',required=True)
    p.add_argument('--fixture',type=Path,action='append',default=[])
    a=p.parse_args();out=a.output_dir.resolve();sdk=a.sdk.resolve()
    validate(a.source.read_text(),a.registration.read_text())
    out.mkdir(parents=True,exist_ok=False)
    original=out/'spu_recomp.c';original.write_text(a.source.read_text())
    shutil.copy2(a.source.with_suffix('.h'),out/'spu_recomp.h')
    exe=out/'replay_job13_capture.exe'
    cmd=[a.compiler,'/nologo','/TC','/std:c17','/O2','/MD','/DSPU_FLOAT_NATIVE_RUNTIME=1',
        '/DSPU_USE_DISPATCH_MT','/D_CRT_SECURE_NO_WARNINGS','/clang:-msse4.1',
        '/clang:-fno-strict-aliasing','/I'+str(sdk/'runtime/spu'),
        '/I'+str(ROOT/'port'),'/I'+str(out),str(ROOT/'tests/replay_job13_capture.c'),
        str(original),str(sdk/'runtime/spu/spu_float_native.c'),'/Fe'+str(exe)]
    result=subprocess.run(cmd,cwd=out,capture_output=True,text=True)
    (out/'compile.log').write_text(result.stdout+result.stderr)
    print(result.stdout+result.stderr,end='')
    if result.returncode:return result.returncode
    env=dict(os.environ)
    for name in ('SPU_FLOAT_NATIVE_VALIDATE','SPU_FLOAT_NATIVE_FILE'):
        env.pop(name,None)
    tests=[]
    for f in a.fixture:
        result=subprocess.run([str(exe),str(f.resolve())],env=env,capture_output=True,text=True,timeout=30)
        tests.append(dict(path=str(f.resolve()),sha256=hashlib.sha256(f.read_bytes()).hexdigest(),
                          exit_code=result.returncode,output=result.stdout+result.stderr))
        print(result.stdout+result.stderr,end='')
    (out/'results.json').write_text(json.dumps(dict(compile_command=cmd,fixtures=tests),indent=2)+'\n')
    if any(t['exit_code'] for t in tests):return 1
    print('READY '+str(exe) if not tests else f'PASS {len(tests)} whole-batch original replays')
    return 0


if __name__=='__main__':sys.exit(main())
