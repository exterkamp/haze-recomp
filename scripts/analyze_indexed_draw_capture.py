"""Validate bounded private draw captures against resolved producer inputs.

This validates the emitted method stream, not GPU output or material/resource
ownership. Refills, partial streams and unsupported methods are never accepted.
"""
import argparse
from collections import Counter
import json
from pathlib import Path
import struct

HEADER=struct.Struct('<8s4IQ')
RECORD=struct.Struct('<2Q20I')
FIELDS='id site lr context begin end cursor final_cursor offset type primitive count alignment flags callbacks_before callbacks_after last_callback last_words payload_bytes reserved'.split()

def decode_stream(payload):
    if not payload or len(payload)%4: raise ValueError('invalid payload length')
    words=struct.unpack('>'+str(len(payload)//4)+'I',payload)
    events=[];i=0
    while i<len(words):
        header=words[i];i+=1
        count=(header>>18)&0x7ff;method=header&0xffff
        if header&~(0x40000000|0x1ffc0000|0xffff) or not count or method&3:
            raise ValueError('unsupported method header')
        if i+count>len(words): raise ValueError('truncated method')
        for j,value in enumerate(words[i:i+count]):
            events.append((method if header&0x40000000 else method+j*4,value))
        i+=count
    if len(events)<6 or events[0]!=(0x1714,0) or events[1][0]!=0x181c or events[2][0]!=0x1820 or events[3][0]!=0x1808 or events[-1]!=(0x1808,0):
        raise ValueError('not one complete indexed draw')
    offset=events[1][1];format_location=events[2][1];primitive=events[3][1]
    if format_location not in (1,17) or primitive not in range(1,11):
        raise ValueError('unsupported index format/location or primitive')
    count=0;segments=[]
    for method,value in events[4:-1]:
        if method!=0x1824: raise ValueError('unexpected operation inside draw')
        first=value&0xffffff;size=(value>>24)+1
        if first!=count: raise ValueError('noncontiguous index range')
        segments.append(size);count+=size
        if count>0x1000000: raise ValueError('index range overflows packet address')
    if not count: raise ValueError('empty indexed draw')
    return dict(offset=offset,type=format_location>>4,location=format_location&15,
                primitive=primitive,count=count,segments=segments,method_events=len(events))

def analyze(path):
    data=path.read_bytes()
    if len(data)<HEADER.size: raise ValueError('truncated file header')
    magic,version,stride,count,capacity,frequency=HEADER.unpack_from(data)
    if magic!=b'HZIDRAW1' or version!=1 or capacity!=4096 or stride!=RECORD.size+capacity or not 0<count<=16384 or not frequency:
        raise ValueError('unsupported capture format')
    if len(data)!=HEADER.size+count*stride: raise ValueError('truncated or trailing capture data')
    result=dict(source=str(path),records=count,accepted=0,rejected=0,flags={},sites={},callers={},rows=[],
                scope='Producer method-stream equivalence only. Does not prove renderer/GPU parity, draw coverage outside this function, resource lifetimes, or an FPS gain.')
    flags=Counter();sites=Counter();callers=Counter()
    for index in range(count):
        at=HEADER.size+index*stride
        raw=RECORD.unpack_from(data,at);r=dict(zip(['qpc_begin','qpc_end']+FIELDS,raw))
        if r['id']!=index+1 or r['reserved'] or r['payload_bytes']>capacity:
            raise ValueError('invalid record identity or payload size')
        r['elapsed_ms']=(r['qpc_end']-r['qpc_begin'])*1000/frequency
        sites[hex(r['site'])]+=1;callers[hex(r['lr'])]+=1
        if r['flags']:
            if r['payload_bytes']: raise ValueError('rejected record exposes a partial payload')
            r['rejection']='capture_flags';flags[str(r['flags'])]+=1
        else:
            try:
                if r['site'] not in (0x731390,0x73167c) or r['callbacks_after'] or r['qpc_end']<r['qpc_begin'] or not r['begin']<=r['cursor']<r['final_cursor']<=r['end'] or r['final_cursor']-r['cursor']!=r['payload_bytes']:
                    raise ValueError('inconsistent contiguous-stream metadata')
                decoded=decode_stream(data[at+RECORD.size:at+RECORD.size+r['payload_bytes']])
                for key in ['offset','type','primitive','count']:
                    if decoded[key]!=r[key]: raise ValueError('resolved input mismatch: '+key)
                prefix=r['alignment'] if 0<r['alignment']<r['count'] else 0
                remaining=r['count']-prefix
                expected=([prefix] if prefix else [])+[256]*(remaining//256)+([remaining%256] if remaining%256 else [])
                if decoded['segments']!=expected: raise ValueError('source segmentation mismatch')
                r['decoded']=decoded
            except ValueError as e: r['rejection']=str(e)
        if 'rejection' in r: result['rejected']+=1
        else: result['accepted']+=1
        result['rows'].append(r)
    result.update(flags=dict(flags),sites=dict(sites),callers=dict(callers))
    return result

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('capture',type=Path);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();r=analyze(a.capture);a.output.write_text(json.dumps(r,indent=2)+'\n')
    print(json.dumps({k:v for k,v in r.items() if k!='rows'},indent=2))
