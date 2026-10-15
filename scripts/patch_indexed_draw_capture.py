"""Instrument a private indexed-draw lift; never changes guest operations."""
import argparse
import hashlib
from pathlib import Path

ENTRY = 'void func_007312B0(ppu_context* ctx) {'
SHA256 = '9553d3bce635d994c91dd90307844a398970a2d1ed7504d4ee7f92cb81264f41'
INCLUDE = '#include "haze_indexed_draw_capture.h"\n'

def read_source(path):
    try: return path.read_text(encoding='utf-8')
    except UnicodeDecodeError: return path.read_text(encoding='cp1252')

def body(source):
    start=source.index(ENTRY)
    if source.count(ENTRY)!=1: raise ValueError('expected one indexed draw function')
    end=source.index('\nvoid func_',start+1)
    original=source[start:end]
    if hashlib.sha256(original.encode()).hexdigest()!=SHA256:
        raise ValueError('indexed draw source changed; re-audit the boundary')
    return original

def patch(source):
    original=body(source)
    modified=original.replace(ENTRY,ENTRY+'\n        haze_indexed_capture::scope haze_draw_capture(ctx->thread_id,ctx->gpr[2],ctx->lr);',1)
    for site in ['00731390','0073167C']:
        label='loc_'+site+':\n'
        assert modified.count(label)==1
        modified=modified.replace(label,label+'        haze_draw_capture.begin(0x'+site+',ctx->gpr[28],ctx->gpr[12],ctx->gpr[31],ctx->gpr[25],ctx->gpr[27],ctx->gpr[26],ctx->gpr[29]);\n',1)
    end='loc_007315E8:\n'
    modified=modified.replace(end,end+'        haze_draw_capture.finish();\n',1)
    callback='        ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);'
    if modified.count(callback)!=6: raise ValueError('callback frontier changed')
    modified=modified.replace(callback,'        haze_draw_capture.callback(ctx->ctr,ctx->gpr[4]);\n'+callback)
    return INCLUDE+source.replace(original,modified,1)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('source',type=Path);p.add_argument('output',type=Path)
    a=p.parse_args();a.output.write_text(patch(read_source(a.source)),encoding='utf-8')
