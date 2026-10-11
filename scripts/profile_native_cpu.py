"""Windows native Haze instruction sampling and per-thread CPU counters.

Usage: python scripts/profile_native_cpu.py PID [SECONDS]
Briefly suspends each Haze thread to read its instruction pointer, then resumes
it before resolving symbols. RIP counts include waiting threads and are NOT
percentages of CPU utilization. CPU milliseconds come from GetThreadTimes.
Requires the matching executable/PDB in build/haze. No guest state is written.
Guest-address counts sample only a recognized fast vm_read32 load instruction.
"""
import ctypes as c
from ctypes import wintypes as w
import sys
import argparse
from pathlib import Path

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('pid',type=int)
parser.add_argument('seconds',type=float,nargs='?',default=20)
parser.add_argument('--scan-every',type=int,default=100,
    help='Thread snapshot cadence in sampling iterations; 1 helps find short jobs but adds overhead.')
args=parser.parse_args()
if not 1<=args.scan_every<=1000:
    parser.error('scan-every must be 1..1000')

k=c.WinDLL('kernel32',use_last_error=True)
d=c.WinDLL('dbghelp',use_last_error=True)
for name in ('OpenProcess','OpenThread'):
    f=getattr(k,name); f.argtypes=[w.DWORD,w.BOOL,w.DWORD]; f.restype=w.HANDLE
k.SuspendThread.argtypes=[w.HANDLE]; k.SuspendThread.restype=w.DWORD
k.ResumeThread.argtypes=[w.HANDLE]
k.CloseHandle.argtypes=[w.HANDLE]
k.GetThreadContext.argtypes=[w.HANDLE,c.c_void_p]
class SYMBOL_INFO(c.Structure):
    _fields_=[('SizeOfStruct',w.ULONG),('TypeIndex',w.ULONG),('Reserved',c.c_ulonglong*2),('Index',w.ULONG),('Size',w.ULONG),('ModBase',c.c_ulonglong),('Flags',w.ULONG),('Value',c.c_ulonglong),('Address',c.c_ulonglong),('Register',w.ULONG),('Scope',w.ULONG),('Tag',w.ULONG),('NameLen',w.ULONG),('MaxNameLen',w.ULONG),('Name',c.c_char*1)]
d.SymInitializeW.argtypes=[w.HANDLE,w.LPCWSTR,w.BOOL]
d.SymFromAddr.argtypes=[w.HANDLE,c.c_ulonglong,c.POINTER(c.c_ulonglong),c.POINTER(SYMBOL_INFO)]
d.SymFromName.argtypes=[w.HANDLE,c.c_char_p,c.POINTER(SYMBOL_INFO)]
h=k.OpenProcess(0x410,False,args.pid); assert h,c.get_last_error()
assert d.SymInitializeW(h,str(Path(__file__).resolve().parents[1]/'build'/'haze'),True),c.get_last_error()
def symbol(addr):
    b=c.create_string_buffer(c.sizeof(SYMBOL_INFO)+2048)
    info=c.cast(b,c.POINTER(SYMBOL_INFO)); info.contents.SizeOfStruct=c.sizeof(SYMBOL_INFO); info.contents.MaxNameLen=2048
    disp=c.c_ulonglong()
    if d.SymFromAddr(h,addr,c.byref(disp),info) and disp.value < 0x10000:
        return c.string_at(c.addressof(b)+SYMBOL_INFO.Name.offset).decode(errors='replace')+f'+0x{disp.value:X}'


import collections,time,json,random
class THREADENTRY32(c.Structure):
    _fields_=[('dwSize',w.DWORD),('cntUsage',w.DWORD),('th32ThreadID',w.DWORD),('th32OwnerProcessID',w.DWORD),('tpBasePri',w.LONG),('tpDeltaPri',w.LONG),('dwFlags',w.DWORD)]
k.CreateToolhelp32Snapshot.argtypes=[w.DWORD,w.DWORD]; k.CreateToolhelp32Snapshot.restype=w.HANDLE
k.Thread32First.argtypes=[w.HANDLE,c.POINTER(THREADENTRY32)]
k.Thread32Next.argtypes=[w.HANDLE,c.POINTER(THREADENTRY32)]
k.GetThreadTimes.argtypes=[w.HANDLE,c.c_void_p,c.c_void_p,c.c_void_p,c.c_void_p]
k.GetProcessTimes.argtypes=k.GetThreadTimes.argtypes
k.GetExitCodeThread.argtypes=[w.HANDLE,c.POINTER(w.DWORD)]
k.GetProcessIdOfThread.argtypes=[w.HANDLE]
k.GetProcessIdOfThread.restype=w.DWORD
k.GetSystemTimeAsFileTime.argtypes=[c.POINTER(c.c_ulonglong)]
pid=args.pid; duration=args.seconds
handles={}; active={}; live_ids={}; first={}; last={}; raw=collections.Counter()
scan_stats=collections.Counter(); seen_ids=set()
started_filetime=c.c_ulonglong()
def running(th):
    code=w.DWORD()
    return bool(k.GetExitCodeThread(th,c.byref(code))) and code.value==259
def scan():
    snap=k.CreateToolhelp32Snapshot(4,0); te=THREADENTRY32(); te.dwSize=c.sizeof(te)
    if snap == c.c_void_p(-1).value: raise c.WinError(c.get_last_error())
    scan_stats['snapshots']+=1
    ok=k.Thread32First(snap,c.byref(te))
    while ok:
        if te.th32OwnerProcessID==pid:
            tid=te.th32ThreadID
            scan_stats['owned_entries']+=1; seen_ids.add(tid)
            old=live_ids.get(tid)
            if old is not None and running(handles[old]):
                ok=k.Thread32Next(snap,c.byref(te)); continue
            if old is not None:
                last[old]=cpu(handles[old]); active.pop(old,None)
            th=k.OpenThread(0x4a,False,tid)
            # IDs can be reused between the snapshot and OpenThread. Never
            # suspend a handle without verifying its process owner.
            if th and k.GetProcessIdOfThread(th)==pid:
                created,exited,kt,ut=[c.c_ulonglong() for _ in range(4)]
                if k.GetThreadTimes(th,c.byref(created),c.byref(exited),c.byref(kt),c.byref(ut)):
                    key=(tid,created.value)
                    if key not in handles:
                        scan_stats['opened_lifetimes']+=1
                        handles[key]=th; active[key]=th; live_ids[tid]=key
                        total=kt.value+ut.value
                        # Include work before first observation when this
                        # thread was created inside the sampling interval.
                        first[key]=0 if created.value>=started_filetime.value else total
                        last[key]=total
                    else: k.CloseHandle(th)
                else:
                    scan_stats['times_failed']+=1; k.CloseHandle(th)
            elif th:
                scan_stats['owner_changed']+=1; k.CloseHandle(th)
            else: scan_stats['open_failed_'+str(c.get_last_error())]+=1
        ok=k.Thread32Next(snap,c.byref(te))
    k.CloseHandle(snap)
def cpu(th):
    a,b,kt,ut=[c.c_ulonglong() for _ in range(4)]
    if not k.GetThreadTimes(th,c.byref(a),c.byref(b),c.byref(kt),c.byref(ut)): return 0
    return kt.value+ut.value
def process_cpu():
    a,b,kt,ut=[c.c_ulonglong() for _ in range(4)]
    if not k.GetProcessTimes(h,c.byref(a),c.byref(b),c.byref(kt),c.byref(ut)):
        raise c.WinError(c.get_last_error())
    return kt.value+ut.value
pathbuf=c.create_unicode_buffer(32768); pathlen=w.DWORD(len(pathbuf))
k.QueryFullProcessImageNameW.argtypes=[w.HANDLE,w.DWORD,w.LPWSTR,c.POINTER(w.DWORD)]
assert k.QueryFullProcessImageNameW(h,0,pathbuf,c.byref(pathlen)),c.get_last_error()
assert Path(pathbuf.value).name.lower()=='hazerecomp.exe', 'PID must identify HazeRecomp.exe'
assert 1<=duration<=120, 'Duration must be between 1 and 120 seconds'
# Recognize this build's ordinary vm_read32 load/bswap pair. RCX holds the
# guest address at these instructions. Do not interpret registers at arbitrary
# helper offsets, where the compiler may have already reused them.
symbuf=c.create_string_buffer(c.sizeof(SYMBOL_INFO)+2048)
syminfo=c.cast(symbuf,c.POINTER(SYMBOL_INFO))
syminfo.contents.SizeOfStruct=c.sizeof(SYMBOL_INFO); syminfo.contents.MaxNameLen=2048
fast_load=0; memory_reads=collections.Counter()
k.ReadProcessMemory.argtypes=[w.HANDLE,c.c_void_p,c.c_void_p,c.c_size_t,c.POINTER(c.c_size_t)]
if d.SymFromName(h,b'vm_read32',syminfo):
    code=c.create_string_buffer(256); got=c.c_size_t()
    address=syminfo.contents.Address
    if k.ReadProcessMemory(h,address,code,len(code),c.byref(got)):
        offset=code.raw[:got.value].find(bytes.fromhex('8b04080fc8'))
        if offset>=0: fast_load=address+offset
k.GetSystemTimeAsFileTime(c.byref(started_filetime))
process_begin=process_cpu()
start=time.perf_counter(); iteration=0
while time.perf_counter()-start<duration:
    if iteration%args.scan_every==0: scan()
    for tid,th in list(active.items()):
        ct=cpu(th); first.setdefault(tid,ct); last[tid]=ct
        if not running(th):
            active.pop(tid,None)
            if live_ids.get(tid[0])==tid: live_ids.pop(tid[0],None)
            continue
        buf=c.create_string_buffer(1250); cp=(c.addressof(buf)+15)&~15
        c.c_uint32.from_address(cp+0x30).value=0x100003  # CONTROL | INTEGER
        if k.SuspendThread(th)==0xffffffff: continue
        try:
            good=k.GetThreadContext(th,cp); rip=c.c_uint64.from_address(cp+0xf8).value
            rcx=c.c_uint64.from_address(cp+0x80).value
        finally: k.ResumeThread(th)
        if good:
            raw[(tid,rip)]+=1
            if fast_load and rip in (fast_load,fast_load+3):
                memory_reads[(tid,rcx&0xffffffff)]+=1
    # Avoid repeatedly sampling the same phase of a bursty per-frame job stream.
    iteration+=1; time.sleep(random.uniform(.004,.014))
elapsed=time.perf_counter()-start
for tid,th in active.items(): last[tid]=cpu(th)
process_ms=(process_cpu()-process_begin)/10000
resolved={}
def sym(addr):
    if addr not in resolved: resolved[addr]=symbol(addr) or hex(addr)
    return resolved[addr]
rows=[]
for tid in sorted(handles,key=lambda t:last[t]-first[t],reverse=True):
    hist=collections.Counter()
    for (t,rip),n in raw.items():
        if t==tid: hist[sym(rip).split('+0x')[0]]+=n
    rows.append(dict(tid=tid[0],created_filetime=tid[1],cpu_ms=(last[tid]-first[tid])/10000,rips=hist.most_common(20),raw_rips=[(sym(rip),n) for (t,rip),n in raw.most_common() if t==tid][:20],
                     sampled_read32_addresses=[(f'0x{addr:08X}',n) for (t,addr),n in memory_reads.most_common() if t==tid][:20]))
tracked_ms=sum(row['cpu_ms'] for row in rows)
print(json.dumps(dict(elapsed_s=elapsed,iterations=iteration,fast_read32_recognized=bool(fast_load),
    scan_every=args.scan_every,process_cpu_ms=process_ms,tracked_thread_cpu_ms=tracked_ms,
    unattributed_cpu_ms=max(0,process_ms-tracked_ms),
    thread_scan=dict(scan_stats,unique_ids=len(seen_ids)),
    coverage_note='Thread snapshots can miss short-lived jobs and CPU used before first observation; RIP samples include waits.',
    threads=rows),indent=2))
for th in handles.values(): k.CloseHandle(th)
k.CloseHandle(h)
