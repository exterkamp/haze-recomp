"""Replay all original batch fixtures and reject damaged boundary evidence."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile

from analyze_job13_capture import analyze,HEADER,EVENT,FIXED_BYTES,FIELDS


def run(exe,path):
    env=dict(os.environ)
    env.pop('SPU_FLOAT_NATIVE_VALIDATE',None);env.pop('SPU_FLOAT_NATIVE_FILE',None)
    r=subprocess.run([str(exe),str(path)],capture_output=True,text=True,timeout=30,env=env)
    return dict(exit_code=r.returncode,output=r.stdout+r.stderr)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--exe',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('fixtures',type=Path,nargs='+');a=p.parse_args()
    exe=a.exe.resolve();results=[];mutations=[]
    for path in a.fixtures:
        info=analyze(path)
        if not info['accepted']:raise ValueError(f'Incomplete capture: {path}')
        attempts=[run(exe,path.resolve()) for _ in range(2)]
        results.append(dict(path=str(path.resolve()),sha256=info['sha256'],attempts=attempts))
        print(json.dumps(results[-1]),flush=True)
    if any(t['exit_code'] for r in results for t in r['attempts']):
        success=False
    else:
        data=a.fixtures[0].read_bytes();h=dict(zip(FIELDS,HEADER.unpack_from(data)[1:]))
        corrupted={}
        def flip(name,offset):
            b=bytearray(data);b[offset]^=1;corrupted[name]=bytes(b)
        flip('final-gpr',128+2048)
        flip('final-ls',128+4096+262144)
        flip('invalid-header',16)
        corrupted['truncated']=data[:-1]
        for path in a.fixtures:
            put_data=path.read_bytes();put_h=dict(zip(FIELDS,HEADER.unpack_from(put_data)[1:]))
            for i in range(put_h['event_count']):
                e=EVENT.unpack_from(put_data,FIXED_BYTES+i*64)
                if e[1]==1 and e[2]==21 and e[3] in (0x20,0x21,0x22) and e[10]:
                    b=bytearray(put_data);b[FIXED_BYTES+put_h['event_count']*64+e[9]]^=1
                    corrupted['put-payload']=bytes(b);break
            if 'put-payload' in corrupted:break
        if 'put-payload' not in corrupted:raise ValueError('No observed PUT to validate output checking')
        # Atomic attempts include payloads even on failure. Corrupt their
        # outputs/results independently so ordinary PUT checks cannot mask a
        # missing completion-queue check.
        for path in a.fixtures:
            atom_data=path.read_bytes();atom_h=dict(zip(FIELDS,HEADER.unpack_from(atom_data)[1:]))
            for i in range(atom_h['event_count']):
                at=FIXED_BYTES+i*64;e=EVENT.unpack_from(atom_data,at)
                if e[1]==1 and e[2]==21 and e[3]==0xd0 and 'atomic-get-status' not in corrupted:
                    b=bytearray(atom_data);struct.pack_into('<I',b,at+60,0)
                    corrupted['atomic-get-status']=bytes(b)
                if e[1]==1 and e[2]==21 and e[3]==0xb4 and 'atomic-put-payload' not in corrupted:
                    b=bytearray(atom_data);b[FIXED_BYTES+atom_h['event_count']*64+e[9]]^=1
                    corrupted['atomic-put-payload']=bytes(b)
                    b=bytearray(atom_data);struct.pack_into('<I',b,at+60,2)
                    corrupted['atomic-put-status']=bytes(b)
            if 'atomic-get-status' in corrupted and 'atomic-put-payload' in corrupted:break
        with tempfile.TemporaryDirectory(prefix='job13-replay-negative-',dir=a.output.parent) as d:
            for name,blob in corrupted.items():
                f=Path(d)/(name+'.bin');f.write_bytes(blob)
                r=run(exe,f);mutations.append(dict(name=name,**r))
                print(json.dumps(mutations[-1]),flush=True)
        # A crash is not a successful validation rejection: require our exit2.
        success=all(r['exit_code']==2 and 'REJECT' in r['output'] for r in mutations)
    a.output.write_text(json.dumps(dict(exe=str(exe),exe_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
        original_replays=results,damaged_fixture_rejections=mutations,success=success,
        scope='Original compute-boundary replay, not a native replacement or game completion/ownership proof.'),indent=2)+'\n')
    return 0 if success else 1


if __name__=='__main__':raise SystemExit(main())
