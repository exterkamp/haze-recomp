"""Prepare guarded actual-source inputs for the isolated PIC04 experiment.

No game/firmware bytes are emitted into the repository. The arithmetic body is
copied from the SDK, with only linkage/name changes and removal of its optional
per-operation audit call. The experiment rejects that audit mode at entry.
"""
from pathlib import Path
import hashlib

LOOP_SHA='957c0836c7290aa1501f246da43aed8d12ccefba5d403ba6cd158772ef0a04f6'
KERNEL_SHA='176f33e06619a69e333ae2535b17a272c8ee01b2a5777d441f76acdca641464f'

def prepare(original: Path, sdk: Path, output: Path):
    source=original.read_text()
    start=source.index('void audio_pic_04_spu_func_0002FA40(')
    end=source.index('\nvoid ',start+1)
    loop=source[start:end]
    if hashlib.sha256(loop.encode()).hexdigest()!=LOOP_SHA:
        raise ValueError('PIC04 loop changed: review original before preparing')
    source=(sdk/'runtime/spu/spu_float_native.c').read_text()
    start=source.index('static inline __attribute__((always_inline)) __m128i canonical(')
    end=source.index('\n#else',start)
    core=source[start:end]
    if hashlib.sha256(core.encode()).hexdigest()!=KERNEL_SHA:
        raise ValueError('Native arithmetic changed: review before preparing')
    core=core.replace('#define NATIVE __attribute__((target("avx512f")))',
        '#define NATIVE static inline __attribute__((always_inline,target("avx512f")))')
    for old,new in [('canonical','haze_filter_canonical'),('flush','haze_filter_flush'),
        ('spu_float_native_add_ps','haze_filter_add_ps'),
        ('spu_float_native_mul_ps','haze_filter_mul_ps'),
        ('spu_float_native_madd_ps','haze_filter_madd_ps')]:
        core=core.replace(old,new)
    for statement in ('if (validate) r=validate_result(r,a,b,(u128){0},0);',
                      'if (validate) r=validate_result(r,a,b,(u128){0},1);',
                      'if (validate) r=validate_result(r,a,b,c,2);'):
        if core.count(statement)!=1: raise ValueError('Arithmetic audit guard changed')
        core=core.replace(statement,'/* Audit mode uses the original loop. */')
    core+='\n#undef NATIVE\n#undef ROUND\n'
    outputs={output/'haze_audio_filter_kernels.generated.h':core,
             output/'audio_filter_original.generated.h':loop}
    if any(path.exists() for path in outputs): raise FileExistsError('Refusing existing oracle output')
    output.mkdir(parents=True,exist_ok=True)
    for path,text in outputs.items(): path.write_text(text,encoding='utf-8',newline='\n')
