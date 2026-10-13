"""Windows native Haze instruction sampling and per-thread CPU counters.

Usage: python scripts/profile_native_cpu.py PID [SECONDS]
Briefly suspends each Haze thread to read its instruction pointer, then resumes
it before resolving output symbols. Optional native stack walking runs while
the thread is suspended and can substantially perturb execution. Never use
this sampler inside FPS benchmark windows. RIP/stack counts include waits and are NOT
percentages of CPU utilization. CPU milliseconds come from GetThreadTimes.
Requires the matching executable/PDB in build/haze or --symbol-path. No guest state is written.
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
parser.add_argument('--tid',type=int,action='append',help='Sample only these thread IDs, after checking process ownership.')
parser.add_argument('--window-thread',action='store_true',
    help='Select the thread owning this process\'s ps3recomp_null window (the native render pump).')
parser.add_argument('--interval-ms',type=float,default=9,
    help='Mean randomized interval between sample rounds; use separately from FPS benchmarks.')
parser.add_argument('--raw-limit',type=int,default=20,help='Maximum individual instruction locations per thread.')
parser.add_argument('--stack-limit',type=int,
    help='Maximum recorded call chains per thread; defaults to raw-limit. Increase to retain rare active callers among many waiting chains.')
parser.add_argument('--symbol-path',type=Path,default=Path(__file__).resolve().parents[1]/'build'/'haze')
parser.add_argument('--stack-depth',type=int,default=0,
    help='Optional AMD64 unwind depth, 0 disables it. Build scripts/native_stackwalk.c first.')
parser.add_argument('--stack-every',type=int,default=10,help='Unwind every N sampling iterations.')
parser.add_argument('--stack-library',type=Path,
    default=Path(__file__).resolve().parents[1]/'local'/'native_stackwalk.dll')
args=parser.parse_args()
if args.window_thread and args.tid:
    parser.error('--window-thread and --tid are mutually exclusive')
if not 1<=args.scan_every<=1000:
    parser.error('scan-every must be 1..1000')
if not .5<=args.interval_ms<=100 or not 1<=args.raw_limit<=1000:
    parser.error('interval-ms must be .5..100 and raw-limit must be 1..1000')
if args.stack_limit is not None and not 1<=args.stack_limit<=10000:
    parser.error('stack-limit must be 1..10000')
if not 0<=args.stack_depth<=64 or not 1<=args.stack_every<=1000:
    parser.error('stack-depth must be 0..64 and stack-every must be 1..1000')
if args.stack_depth and not args.stack_library.is_file():
    parser.error('Build the native stackwalk helper DLL before requesting stacks')

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
assert d.SymInitializeW(h,str(args.symbol_path.resolve()),True),c.get_last_error()
stackwalk=None
if args.stack_depth:
    stack_library=c.WinDLL(str(args.stack_library.resolve()),use_last_error=True)
    stackwalk=stack_library.haze_profile_stackwalk
    stackwalk.argtypes=[w.HANDLE,w.HANDLE,c.c_void_p,c.POINTER(c.c_ulonglong),w.UINT]
    stackwalk.restype=w.UINT
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
stacks=collections.Counter(); suspension_count=0; suspension_total=0; suspension_max=0
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
        if te.th32OwnerProcessID==pid and (not args.tid or te.th32ThreadID in args.tid):
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
window_thread_evidence=None
if args.window_thread:
    user=c.WinDLL('user32',use_last_error=True)
    callback_type=c.WINFUNCTYPE(w.BOOL,w.HWND,w.LPARAM)
    user.EnumWindows.argtypes=[callback_type,w.LPARAM]
    user.GetWindowThreadProcessId.argtypes=[w.HWND,c.POINTER(w.DWORD)]
    user.GetWindowThreadProcessId.restype=w.DWORD
    user.GetClassNameW.argtypes=[w.HWND,w.LPWSTR,c.c_int]
    matches=[]
    @callback_type
    def visit(window,param):
        owner=w.DWORD()
        tid=user.GetWindowThreadProcessId(window,c.byref(owner))
        name=c.create_unicode_buffer(256)
        if owner.value==pid and user.GetClassNameW(window,name,len(name)) and name.value=='ps3recomp_null':
            matches.append(dict(hwnd=int(window),tid=int(tid),pid=int(owner.value),window_class=name.value))
        return True
    if not user.EnumWindows(visit,0):raise c.WinError(c.get_last_error())
    if len(matches)!=1:raise RuntimeError(f'Expected one owned renderer window, found {len(matches)}')
    window_thread_evidence=matches[0]
    args.tid=[window_thread_evidence['tid']]
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
        suspended_begin=time.perf_counter()
        try:
            good=k.GetThreadContext(th,cp); rip=c.c_uint64.from_address(cp+0xf8).value
            rcx=c.c_uint64.from_address(cp+0x80).value
            if good and stackwalk and iteration%args.stack_every==0:
                output=(c.c_ulonglong*args.stack_depth)()
                count=stackwalk(h,th,cp,output,args.stack_depth)
                stacks[(tid,tuple(output[:count]))]+=1
        finally:
            k.ResumeThread(th)
            suspended_elapsed=time.perf_counter()-suspended_begin
            suspension_count+=1; suspension_total+=suspended_elapsed
            suspension_max=max(suspension_max,suspended_elapsed)
        if good:
            raw[(tid,rip)]+=1
            if fast_load and rip in (fast_load,fast_load+3):
                memory_reads[(tid,rcx&0xffffffff)]+=1
    # Avoid repeatedly sampling the same phase of a bursty per-frame job stream.
    iteration+=1; time.sleep(random.uniform(.5,1.5)*args.interval_ms/1000)
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
    rows.append(dict(tid=tid[0],created_filetime=tid[1],cpu_ms=(last[tid]-first[tid])/10000,rips=hist.most_common(20),raw_rips=[(sym(rip),n) for (t,rip),n in raw.most_common() if t==tid][:args.raw_limit],
                     stacks=[dict(frames=[sym(pc) for pc in addresses],samples=n)
                             for (t,addresses),n in stacks.most_common() if t==tid][:(args.stack_limit or args.raw_limit)],
                     sampled_read32_addresses=[(f'0x{addr:08X}',n) for (t,addr),n in memory_reads.most_common() if t==tid][:20]))
tracked_ms=sum(row['cpu_ms'] for row in rows)
print(json.dumps(dict(pid=pid,exe=pathbuf.value,elapsed_s=elapsed,iterations=iteration,fast_read32_recognized=bool(fast_load),
    scan_every=args.scan_every,selected_tids=args.tid,window_thread=window_thread_evidence,interval_ms=args.interval_ms,
    symbol_path=str(args.symbol_path.resolve()),stack_depth=args.stack_depth,stack_every=args.stack_every,
    suspension=dict(count=suspension_count,total_ms=suspension_total*1000,max_ms=suspension_max*1000),
    process_cpu_ms=process_ms,tracked_thread_cpu_ms=tracked_ms,
    unattributed_cpu_ms=max(0,process_ms-tracked_ms),
    thread_scan=dict(scan_stats,unique_ids=len(seen_ids)),
    coverage_note='Thread snapshots can miss short-lived jobs and CPU used before first observation; RIP samples include waits.',
    threads=rows),indent=2))
for th in handles.values(): k.CloseHandle(th)
k.CloseHandle(h)
