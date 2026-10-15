"""Validate private batch transcripts and describe their actual input classes.

No game bytes are printed. A consistent sequential memory transcript is useful
for an offline native algorithm; it does not prove exclusive ownership in game.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct

HEADER=struct.Struct('<8s24I3Q')
EVENT=struct.Struct('<11I16sI')
FIELDS=('version header_size valid invalid_reason image_id initial_pc final_pc '
        'initial_status final_status initial_mxcsr final_mxcsr event_count event_calls '
        'payload_size initial_lsa initial_eah initial_eal initial_size initial_tag '
        'initial_tag_mask initial_tag_status host_tid batch_index initial_atomic_stat '
        'qpc_frequency begin_qpc end_qpc').split()
FIXED_BYTES=128+2*2048+2*262144


def analyze(path):
    data=path.read_bytes()
    if len(data)<HEADER.size:raise ValueError('truncated header')
    values=HEADER.unpack_from(data)
    h=dict(zip(FIELDS,values[1:]))
    if values[0]!=b'HZJOB13\0' or h['version']!=2 or h['header_size']!=128:
        raise ValueError('unsupported header')
    count=h['event_count'];size=h['payload_size']
    if count>8192 or size>4*1024*1024 or len(data)!=FIXED_BYTES+count*64+size:
        raise ValueError('invalid file bounds')
    if h['valid']!=1 or h['invalid_reason'] or h['event_calls']!=count:
        return dict(path=str(path),sha256=hashlib.sha256(data).hexdigest(),header=h,accepted=False)
    if not count or h['image_id']!=32 or h['final_pc']!=0xCE04 or not h['qpc_frequency'] or h['end_qpc']<h['begin_qpc']:
        raise ValueError('invalid batch identity or continuation')
    payload=data[FIXED_BYTES+count*64:]
    known={};initial={};changed_reads=0;examples=[];gets=puts=bytes_get=bytes_put=0
    objects=[];headers=[];commands=Counter();end_payload=0
    atomic_status=h['initial_atomic_stat'];reservation=None;atomic_failures=0;atomic_changed_reads=0
    for i in range(count):
        seq,kind,ch,cmd,lsa,eah,eal,n,tag,off,length,value,status=EVENT.unpack_from(data,FIXED_BYTES+i*64)
        atomic=kind==1 and ch==21 and cmd in (0xd0,0xb4)
        if seq!=i or kind not in (1,2,3) or (status and not atomic):raise ValueError('invalid event sequence/status')
        if not length:
            if off!=0xFFFFFFFF:raise ValueError('noncanonical empty payload')
        else:
            if off!=end_payload or length>size-off:raise ValueError('noncanonical payload')
            end_payload+=length
        if ch!=21 or kind!=1:
            if length:raise ValueError('payload on non-DMA event')
            if kind in (2,3) and ch not in (12,24,27):raise ValueError('unsupported channel read/count')
            if kind==2 and ch==27:
                if atomic_status is None or struct.unpack_from('<I',value)[0]!=atomic_status:
                    raise ValueError('atomic result disagrees with recorded operation')
            continue
        if struct.unpack_from('<I',value)[0]!=cmd:raise ValueError('DMA command disagrees with channel value')
        if atomic:
            lsa &= 262143
            n=128;ea=eal & ~127
            if cmd==0xd0:
                if status!=4:raise ValueError('invalid GETLLAR status')
                reservation=ea
            else:
                if atomic_status is None or status not in (0,1):raise ValueError('invalid PUTLLC status/order')
                if status==0 and reservation!=ea:raise ValueError('PUTLLC success without reservation')
                reservation=None
                atomic_failures+=status
            atomic_status=status
        else:
            if cmd not in (0x20,0x21,0x22,0x40,0x41,0x42):raise ValueError('unsupported DMA command')
            ea=(eah<<32)|eal
        if n!=length or lsa>262144 or n>262144-lsa:raise ValueError('unsupported DMA span')
        commands[hex(cmd)]+=1
        blob=payload[off:off+length] if length else b''
        if ea+n>2**64:raise ValueError('DMA address overflow')
        if cmd in (0x40,0x41,0x42,0xd0):
            gets+=1;bytes_get+=n
            for k,v in enumerate(blob):
                addr=ea+k
                if addr in known and known[addr]!=v:
                    changed_reads+=1
                    if atomic:atomic_changed_reads+=1
                    if len(examples)<8:examples.append(dict(event=i,address=hex(addr)))
                if addr not in known:initial[addr]=v
                known[addr]=v
            if not atomic and n==5200:
                b=blob[16:]
                headers.append(dict(event=i,slot_ea=hex(ea),aux_masks=[hex(v) for v in struct.unpack_from('>II',b)],
                    auxiliary_ea=hex(struct.unpack_from('>I',b,0x10)[0]),
                    object_list_ea=hex(struct.unpack_from('>I',b,0x14)[0]),
                    timestep=struct.unpack_from('>f',b,0x18)[0],object_count=struct.unpack_from('>I',b,0x1c)[0]))
            if not atomic and n==1088:
                get=lambda offset:struct.unpack_from('>I',blob,offset)[0]
                objects.append(dict(event=i,ea=hex(ea),particle_ea=hex(get(0x3a8)),
                    capacity=get(0x414),live=get(0x418),field_41c=get(0x41c),
                    ring_cursor=get(0x424),state=get(0x428),flags=hex(get(0x430)),
                    behavior_flags=hex(get(0x438))))
        elif not atomic or status==0:
            puts+=1;bytes_put+=n
            for k,v in enumerate(blob):known[ea+k]=v
    if end_payload!=size:raise ValueError('unused payload')
    return dict(path=str(path),sha256=hashlib.sha256(data).hexdigest(),header=h,accepted=True,
        elapsed_diagnostic_ms=1000*(h['end_qpc']-h['begin_qpc'])/h['qpc_frequency'],
        dma_commands=dict(commands),gets=gets,puts=puts,get_bytes=bytes_get,put_bytes=bytes_put,
        initial_known_bytes=len(initial),sequential_memory_consistent=changed_reads==0,
        externally_changed_get_bytes=changed_reads,changed_read_examples=examples,
        atomic_failed_put_attempts=atomic_failures,externally_changed_atomic_get_bytes=atomic_changed_reads,
        externally_changed_ordinary_get_bytes=changed_reads-atomic_changed_reads,
        batch_headers=headers,object_gets=objects,
        note='Sizes/offsets are audited candidate layouts. Whole-batch replay is separate. Sequential consistency does not prove cross-job ownership; elapsed capture time includes diagnostic overhead.')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('fixtures',type=Path,nargs='+');p.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    result=[analyze(f) for f in a.fixtures]
    a.output.write_text(json.dumps(result,indent=2)+'\n')
    for r in result:
        print(json.dumps({k:v for k,v in r.items() if k not in ('header','object_gets','changed_read_examples')}))
