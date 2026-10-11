"""Sample a compiled SPU image using live job-start IDs, avoiding stale snapshots.

Build with matching PDB; run muted with SPU_JOB_TIMING=1 and
SPU_JOB_TIMING_IMAGE=<image>. Samples briefly suspend only that image's current
host thread. Counts include runtime helpers/waits, not CPU utilization.
"""
import argparse
import collections
import ctypes as c
from ctypes import wintypes as w
import json
from pathlib import Path
import re
import time

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('pid',type=int)
p.add_argument('log',type=Path)
p.add_argument('--image',type=int,default=40)
p.add_argument('--seconds',type=float,default=20)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args()
if not 1<=a.seconds<=120: p.error('seconds must be 1..120')
k=c.WinDLL('kernel32',use_last_error=True); d=c.WinDLL('dbghelp',use_last_error=True)
for name in ('OpenProcess','OpenThread'):
    f=getattr(k,name); f.argtypes=[w.DWORD,w.BOOL,w.DWORD]; f.restype=w.HANDLE
k.GetProcessIdOfThread.argtypes=[w.HANDLE]; k.GetProcessIdOfThread.restype=w.DWORD
k.CloseHandle.argtypes=[w.HANDLE]
k.SuspendThread.argtypes=[w.HANDLE]; k.SuspendThread.restype=w.DWORD
k.ResumeThread.argtypes=[w.HANDLE]
k.GetThreadContext.argtypes=[w.HANDLE,c.c_void_p]
k.GetExitCodeThread.argtypes=[w.HANDLE,c.POINTER(w.DWORD)]
k.GetThreadDescription.argtypes=[w.HANDLE,c.POINTER(w.LPWSTR)]
k.LocalFree.argtypes=[c.c_void_p]
k.QueryFullProcessImageNameW.argtypes=[w.HANDLE,w.DWORD,w.LPWSTR,c.POINTER(w.DWORD)]
class Symbol(c.Structure):
    _fields_=[('SizeOfStruct',w.ULONG),('TypeIndex',w.ULONG),('Reserved',c.c_ulonglong*2),
        ('Index',w.ULONG),('Size',w.ULONG),('ModBase',c.c_ulonglong),('Flags',w.ULONG),
        ('Value',c.c_ulonglong),('Address',c.c_ulonglong),('Register',w.ULONG),
        ('Scope',w.ULONG),('Tag',w.ULONG),('NameLen',w.ULONG),('MaxNameLen',w.ULONG),('Name',c.c_char*1)]
d.SymInitializeW.argtypes=[w.HANDLE,w.LPCWSTR,w.BOOL]
d.SymFromAddr.argtypes=[w.HANDLE,c.c_ulonglong,c.POINTER(c.c_ulonglong),c.POINTER(Symbol)]
d.SymCleanup.argtypes=[w.HANDLE]
process=k.OpenProcess(0x410,False,a.pid)
if not process: raise c.WinError(c.get_last_error())
active={}; samples=collections.Counter(); stats=collections.Counter()
try:
    name=c.create_unicode_buffer(32768); length=w.DWORD(len(name))
    if not k.QueryFullProcessImageNameW(process,0,name,c.byref(length)) or Path(name.value).name.lower()!='hazerecomp.exe':
        raise RuntimeError('PID must identify HazeRecomp.exe')
    if not d.SymInitializeW(process,str(Path(__file__).resolve().parents[1]/'build'/'haze'),True):
        raise c.WinError(c.get_last_error())
    with a.log.open('rb') as log:
        log.seek(0,2); pending=b''; start=time.perf_counter()
        while time.perf_counter()-start<a.seconds:
            pending+=log.read()
            lines=pending.split(b'\n'); pending=lines.pop()
            for line in lines:
                if not line.startswith(b'[spu-job-start] '): continue
                fields=dict(re.findall(rb'(\w+)=(\w+)',line))
                if int(fields[b'image'])!=a.image: continue
                stats['announced_jobs']+=1; tid=int(fields[b'tid'])
                if tid in active: continue
                thread=k.OpenThread(0x4a,False,tid)
                if not thread: stats['open_failed']+=1; continue
                if k.GetProcessIdOfThread(thread)!=a.pid:
                    stats['owner_changed']+=1; k.CloseHandle(thread); continue
                description=w.LPWSTR()
                result=k.GetThreadDescription(thread,c.byref(description))
                correct=result>=0 and description.value==f'spu-image-{a.image}'
                if description: k.LocalFree(c.cast(description,c.c_void_p))
                if not correct:
                    stats['identity_changed']+=1; k.CloseHandle(thread); continue
                active[tid]=thread; stats['opened_jobs']+=1
            for tid,thread in list(active.items()):
                code=w.DWORD()
                if not k.GetExitCodeThread(thread,c.byref(code)) or code.value!=259:
                    k.CloseHandle(thread); del active[tid]; continue
                buffer=c.create_string_buffer(1250); ptr=(c.addressof(buffer)+15)&~15
                c.c_uint32.from_address(ptr+0x30).value=0x100003
                if k.SuspendThread(thread)==0xffffffff: continue
                try:
                    if k.GetThreadContext(thread,ptr):
                        samples[c.c_uint64.from_address(ptr+0xf8).value]+=1
                finally: k.ResumeThread(thread)
            time.sleep(.002)
    elapsed=time.perf_counter()-start; resolved=collections.Counter(); raw=[]
    for address,count in samples.most_common():
        buffer=c.create_string_buffer(c.sizeof(Symbol)+2048)
        info=c.cast(buffer,c.POINTER(Symbol)); info.contents.SizeOfStruct=c.sizeof(Symbol); info.contents.MaxNameLen=2048
        displacement=c.c_ulonglong()
        symbol=hex(address)
        if d.SymFromAddr(process,address,c.byref(displacement),info) and displacement.value<0x10000:
            symbol=c.string_at(c.addressof(buffer)+Symbol.Name.offset).decode(errors='replace')
        resolved[symbol]+=count; raw.append(dict(address=hex(address),symbol=symbol,offset=displacement.value,count=count))
    output=dict(image=a.image,elapsed_s=elapsed,stats=dict(stats),samples=sum(samples.values()),
        symbols=resolved.most_common(),raw=raw,note=__doc__)
    a.output.write_text(json.dumps(output,indent=2))
    print(json.dumps({key:output[key] for key in ('image','elapsed_s','stats','samples','symbols')},indent=2))
finally:
    for thread in active.values(): k.CloseHandle(thread)
    d.SymCleanup(process); k.CloseHandle(process)
