"""Compile and check the capture plus the private original draw emitter.

Run in a Visual Studio developer environment with --compiler pointing to
clang-cl. No generated game source or captures are added to the repository.
"""
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
import patch_indexed_draw_capture as patcher
from analyze_indexed_draw_capture import decode_stream

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--compiler',required=True)
    p.add_argument('--source',type=Path,required=True)
    p.add_argument('--output-dir',type=Path,required=True)
    a=p.parse_args();out=a.output_dir.resolve();out.mkdir(parents=True,exist_ok=False)
    original=patcher.body(patcher.read_source(a.source))
    (out/'indexed_draw_original.inc').write_text(original,encoding='utf-8')
    def run(command,name,extra=None):
        env=os.environ.copy();env.update(extra or {})
        result=subprocess.run(command,cwd=out,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
        (out/(name+'.log')).write_text(result.stdout,encoding='utf-8')
        if result.returncode: raise RuntimeError(name+': '+result.stdout)
    run([a.compiler,'/nologo','/std:c++17','/EHsc','/O2','/DHAZE_INDEXED_CAPTURE_CAPACITY=6','/I'+str(ROOT/'port'),str(ROOT/'port/haze_indexed_draw_capture.cpp'),str(ROOT/'tests/test_indexed_draw_capture.cpp'),'/Fe'+str(out/'capture.exe')],'capture-build')
    (out/'gate').write_text('1')
    run([str(out/'capture.exe')],'capture-run',{'HAZE_INDEXED_CAPTURE_OUT':str(out/'capture.bin'),'HAZE_INDEXED_CAPTURE_GATE':str(out/'gate')})
    run([sys.executable,str(ROOT/'tests/test_indexed_draw_capture.py')],'capture-python',{'HAZE_INDEXED_ORACLE_SOURCE':str(a.source.resolve()),'HAZE_INDEXED_FIXTURE_CAPTURE':str(out/'capture.bin')})
    run([a.compiler,'/nologo','/std:c++17','/EHsc','/O2','/I'+str(out),str(ROOT/'tests/test_indexed_draw_oracle.cpp'),'/Fe'+str(out/'oracle.exe')],'oracle-build')
    run([str(out/'oracle.exe'),str(out/'original-streams.bin')],'oracle-run')
    data=(out/'original-streams.bin').read_bytes();at=0;cases=[]
    while at<len(data):
        mode,typ,count,offset,size=struct.unpack_from('<5I',data,at);at+=20
        decoded=decode_stream(data[at:at+size]);at+=size
        assert (decoded['offset'],decoded['type'],decoded['count'],decoded['primitive'],decoded['location'])==(offset,typ,count,5,1)
        alignment=(((offset+127)&~127)-offset)>>(1 if typ else 2)
        prefix=alignment if 0<alignment<count else 0;remaining=count-prefix
        expected=([prefix] if prefix else [])+[256]*(remaining//256)+([remaining%256] if remaining%256 else [])
        assert decoded['segments']==expected,(mode,typ,count,offset)
        cases.append(dict(mode=mode,type=typ,count=count,offset=offset,bytes=size,segments=len(decoded['segments'])))
    assert at==len(data) and len(cases)==256
    (out/'oracle-results.json').write_text(json.dumps(dict(cases=len(cases),passed=True,rows=cases),indent=2)+'\n')
    print('PASS capture guards and 256 actual-source draw streams (both address paths, index formats, alignment and large packet boundaries)')

if __name__=='__main__': main()
