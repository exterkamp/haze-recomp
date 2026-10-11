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
from pathlib import Path

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
h=k.OpenProcess(0x410,False,int(sys.argv[1])); assert h,c.get_last_error()
assert d.SymInitializeW(h,str(Path(__file__).resolve().parents[1]/'build'/'haze'),True),c.get_last_error()
def symbol(addr):
    b=c.create_string_buffer(c.sizeof(SYMBOL_INFO)+2048)
    info=c.cast(b,c.POINTER(SYMBOL_INFO)); info.contents.SizeOfStruct=c.sizeof(SYMBOL_INFO); info.contents.MaxNameLen=2048
    disp=c.c_ulonglong()
    if d.SymFromAddr(h,addr,c.byref(disp),info) and disp.value < 0x10000:
        return c.string_at(c.addressof(b)+SYMBOL_INFO.Name.offset).decode(errors='replace')+f'+0x{disp.value:X}'


import collections,time,json
class THREADENTRY32(c.Structure):
    _fields_=[('dwSize',w.DWORD),('cntUsage',w.DWORD),('th32ThreadID',w.DWORD),('th32OwnerProcessID',w.DWORD),('tpBasePri',w.LONG),('tpDeltaPri',w.LONG),('dwFlags',w.DWORD)]
k.CreateToolhelp32Snapshot.argtypes=[w.DWORD,w.DWORD]; k.CreateToolhelp32Snapshot.restype=w.HANDLE
k.Thread32First.argtypes=[w.HANDLE,c.POINTER(THREADENTRY32)]
k.Thread32Next.argtypes=[w.HANDLE,c.POINTER(THREADENTRY32)]
k.GetThreadTimes.argtypes=[w.HANDLE,c.c_void_p,c.c_void_p,c.c_void_p,c.c_void_p]
pid=int(sys.argv[1]); duration=float(sys.argv[2]) if len(sys.argv)>2 else 20
handles={}; first={}; last={}; raw=collections.Counter()
def scan():
    snap=k.CreateToolhelp32Snapshot(4,0); te=THREADENTRY32(); te.dwSize=c.sizeof(te)
    ok=k.Thread32First(snap,c.byref(te))
    while ok:
        if te.th32OwnerProcessID==pid and te.th32ThreadID not in handles:
            th=k.OpenThread(0x4a,False,te.th32ThreadID)
            if th: handles[te.th32ThreadID]=th
        ok=k.Thread32Next(snap,c.byref(te))
    k.CloseHandle(snap)
def cpu(th):
    a,b,kt,ut=[c.c_ulonglong() for _ in range(4)]
    if not k.GetThreadTimes(th,c.byref(a),c.byref(b),c.byref(kt),c.byref(ut)): return 0
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
start=time.perf_counter(); iteration=0
while time.perf_counter()-start<duration:
    if iteration%100==0: scan()
    for tid,th in handles.items():
        ct=cpu(th); first.setdefault(tid,ct); last[tid]=ct
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
    iteration+=1; time.sleep(.009)
elapsed=time.perf_counter()-start
resolved={}
def sym(addr):
    if addr not in resolved: resolved[addr]=symbol(addr) or hex(addr)
    return resolved[addr]
rows=[]
for tid in sorted(handles,key=lambda t:last[t]-first[t],reverse=True):
    hist=collections.Counter()
    for (t,rip),n in raw.items():
        if t==tid: hist[sym(rip).split('+0x')[0]]+=n
    rows.append(dict(tid=tid,cpu_ms=(last[tid]-first[tid])/10000,rips=hist.most_common(20),raw_rips=[(sym(rip),n) for (t,rip),n in raw.most_common() if t==tid][:20],
                     sampled_read32_addresses=[(f'0x{addr:08X}',n) for (t,addr),n in memory_reads.most_common() if t==tid][:20]))
print(json.dumps(dict(elapsed_s=elapsed,iterations=iteration,fast_read32_recognized=bool(fast_load),threads=rows),indent=2))
for th in handles.values(): k.CloseHandle(th)
k.CloseHandle(h)
