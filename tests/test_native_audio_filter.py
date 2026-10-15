"""Standalone actual-source differential oracle; no game or renderer needed.

Run inside the VS x64 environment, with CLANG_CL set if clang-cl is not on PATH:
python tests/test_native_audio_filter.py --sdk <readonly SDK> --original <PIC04 source>
An optional --benchmark runs loop-only CPU cycles after parity passes.
"""
from pathlib import Path
import argparse,os,shutil,subprocess,sys,tempfile
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
from prepare_audio_filter_oracle import prepare

def check_guards(original,sdk,stage):
    changed=stage/'changed_original.c'
    changed.write_text(original.read_text().replace(
        'void audio_pic_04_spu_func_0002FA40(spu_context* ctx) {',
        'void audio_pic_04_spu_func_0002FA40(spu_context* ctx) { /* changed */',1))
    changed_sdk=stage/'changed-sdk'
    kernel=changed_sdk/'runtime/spu/spu_float_native.c'
    kernel.parent.mkdir(parents=True)
    kernel.write_text((sdk/'runtime/spu/spu_float_native.c').read_text().replace(
        'static inline __attribute__((always_inline)) __m128i canonical(',
        '/* changed */\nstatic inline __attribute__((always_inline)) __m128i canonical(',1).replace(
        'return _mm_andnot_si128(_mm_cmpeq_epi32(exponent,_mm_setzero_si128()),x);',
        'return _mm_andnot_si128(_mm_cmpeq_epi32(exponent,_mm_setzero_si128()),x); /* changed */',1))
    for source,library in [(changed,sdk),(original,changed_sdk)]:
        output=stage/'must-not-exist'
        try: prepare(source,library,output)
        except ValueError: pass
        else: raise AssertionError('changed actual source accepted')
        assert not output.exists()
    print('PASS changed-loop and changed-arithmetic guards reject before output')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk',type=Path,required=True)
    parser.add_argument('--original',type=Path,required=True)
    parser.add_argument('--benchmark',action='store_true')
    parser.add_argument('--portable-only',action='store_true',help='Compile without native ISA/runtime support')
    parser.add_argument('--check-guards-only',action='store_true')
    args=parser.parse_args()
    sdk=args.sdk.resolve();original=args.original.resolve()
    with tempfile.TemporaryDirectory(prefix='haze-audio-filter-') as directory:
        stage=Path(directory);check_guards(original,sdk,stage)
        if args.check_guards_only: return 0
        compiler=os.getenv('CLANG_CL') or shutil.which('clang-cl')
        if not compiler: raise RuntimeError('clang-cl unavailable')
        prepare(original,sdk,stage)
        # A changed private loop/kernel must fail closed; existing outputs must
        # never be overwritten or recursively removed by the preparer.
        try: prepare(original,sdk,stage)
        except FileExistsError: pass
        else: raise AssertionError('existing output accepted')
        exe=stage/'test_native_audio_filter.exe'
        cmd=[compiler,'/nologo','/TC','/W4','/WX','/O2','/clang:-O2',
             '/clang:-msse4.1','/clang:-ffp-contract=off',
             '/D_CRT_SECURE_NO_WARNINGS',
             '/I'+str(sdk/'runtime/spu'),'/I'+str(ROOT/'port'),'/I'+str(stage),
             str(ROOT/'tests/test_native_audio_filter.c'),str(ROOT/'port/haze_audio_filter.c'),
             str(sdk/'runtime/spu/spu_float_native.c'),'/Fe'+str(exe)]
        if not args.portable_only:cmd.insert(1,'/DSPU_FLOAT_NATIVE_RUNTIME=1')
        result=subprocess.run(cmd,cwd=stage,capture_output=True,text=True)
        print(result.stdout+result.stderr,end='');print('compile_exit='+str(result.returncode))
        if result.returncode:return result.returncode
        environment=dict(os.environ);environment.pop('SPU_FLOAT_NATIVE_VALIDATE',None)
        environment.pop('SPU_FLOAT_NATIVE_FILE',None)
        for extra,env in [(['--benchmark'] if args.benchmark else [],environment),
                          (['--audit-mode'],dict(environment,SPU_FLOAT_NATIVE_VALIDATE='1'))]:
            result=subprocess.run([str(exe),*extra],env=env,capture_output=True,text=True,timeout=120)
            print(result.stdout+result.stderr,end='');print('fixture_exit='+str(result.returncode))
            if result.returncode:return result.returncode
    print('TEST_EXIT=0');return 0
if __name__=='__main__':sys.exit(main())
