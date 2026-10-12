"""Read Haze's candidate frame-clock globals without suspending or writing it.

Addresses come from BLUS30094's lifted 0068ECD8/0068EC78 routines, TOC E94EE0.
These are clock leads, not proof that every gameplay system consumes this delta.
Use separately from FPS benchmarking. Output retains raw samples and pointers.
"""
import argparse
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
from pathlib import Path
import struct
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('pid', type=int)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--seconds', type=float, default=10)
    args = parser.parse_args()
    if not 1 <= args.seconds <= 60 or args.output.exists():
        parser.error('Use 1..60 seconds and a new output path')
    kernel = c.WinDLL('kernel32', use_last_error=True)
    dbg = c.WinDLL('dbghelp', use_last_error=True)
    kernel.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
    kernel.OpenProcess.restype = w.HANDLE
    kernel.CloseHandle.argtypes = [w.HANDLE]
    kernel.ReadProcessMemory.argtypes = [w.HANDLE,c.c_void_p,c.c_void_p,c.c_size_t,c.POINTER(c.c_size_t)]
    kernel.QueryFullProcessImageNameW.argtypes = [w.HANDLE,w.DWORD,w.LPWSTR,c.POINTER(w.DWORD)]
    kernel.GetExitCodeProcess.argtypes = [w.HANDLE,c.POINTER(w.DWORD)]

    class Symbol(c.Structure):
        _fields_ = [('SizeOfStruct',w.ULONG),('TypeIndex',w.ULONG),('Reserved',c.c_ulonglong*2),
                    ('Index',w.ULONG),('Size',w.ULONG),('ModBase',c.c_ulonglong),('Flags',w.ULONG),
                    ('Value',c.c_ulonglong),('Address',c.c_ulonglong),('Register',w.ULONG),
                    ('Scope',w.ULONG),('Tag',w.ULONG),('NameLen',w.ULONG),('MaxNameLen',w.ULONG),('Name',c.c_char*1)]
    dbg.SymInitializeW.argtypes = [w.HANDLE,w.LPCWSTR,w.BOOL]
    dbg.SymFromName.argtypes = [w.HANDLE,c.c_char_p,c.POINTER(Symbol)]
    dbg.SymCleanup.argtypes = [w.HANDLE]
    handle = kernel.OpenProcess(0x410, False, args.pid)
    if not handle:
        raise c.WinError(c.get_last_error())
    symbols = False
    try:
        path = c.create_unicode_buffer(32768); length = w.DWORD(len(path))
        if not kernel.QueryFullProcessImageNameW(handle,0,path,c.byref(length)):
            raise c.WinError(c.get_last_error())
        if Path(path.value).resolve() != args.exe.resolve() or args.exe.name.lower() != 'hazerecomp.exe':
            raise RuntimeError('PID does not match the explicitly owned Haze executable')
        symbols = bool(dbg.SymInitializeW(handle,str(args.exe.resolve().parent),True))
        if not symbols:
            raise c.WinError(c.get_last_error())
        storage = c.create_string_buffer(c.sizeof(Symbol)+2048)
        symbol = c.cast(storage,c.POINTER(Symbol))
        symbol.contents.SizeOfStruct = c.sizeof(Symbol); symbol.contents.MaxNameLen = 2048
        if not dbg.SymFromName(handle,b'vm_base',symbol):
            raise RuntimeError('Matching PDB with vm_base is required')

        def read(address, count):
            buffer = c.create_string_buffer(count); got = c.c_size_t()
            if not kernel.ReadProcessMemory(handle,address,buffer,count,c.byref(got)) or got.value != count:
                raise RuntimeError(f'Cannot read {count} bytes at {address:X}')
            return buffer.raw

        vm = struct.unpack('<Q',read(symbol.contents.Address,8))[0]
        def word(ea):
            if not 0x10000 <= ea <= 0xFFFFFFFC:
                raise RuntimeError(f'Invalid guest pointer {ea:X}')
            return struct.unpack('>I',read(vm+ea,4))[0]
        base = word(0x00E94EE0-0x7D44)
        if base != 0x00E35F10:
            raise RuntimeError('Unsupported BLUS30094 clock TOC layout')
        fields = {'accumulator':(-0x7FF0,'d'), 'delta':(-0x7FE0,'f'),
                  'raw_delta':(-0x7FE4,'f'), 'last_clock':(-0x7FE8,'d'),
                  'frame_counter':(-0x7FB8,'I'), 'correction':(-0x7FD8,'d')}
        pointers = {name:word(base+offset) for name,(offset,_) in fields.items()}
        expected = dict(accumulator=0x01515318,delta=0x01515320,raw_delta=0x01515330,
                        last_clock=0x01515328,frame_counter=0x01515324,correction=0x012B9C08)
        if pointers != expected:
            raise RuntimeError('Unsupported or uninitialized Haze frame-clock layout')
        constants = {hex(offset):read(vm+base+offset,size).hex()
                     for offset,size in [(-0x7FB0,8),(-0x7FD4,4),(-0x7FA8,8)]}
        rows = []; start = time.perf_counter()
        while time.perf_counter()-start < args.seconds:
            exit_code = w.DWORD()
            if not kernel.GetExitCodeProcess(handle,c.byref(exit_code)) or exit_code.value != 259:
                raise RuntimeError('Owned process exited during clock sampling')
            qpc = c.c_longlong(); kernel.QueryPerformanceCounter(c.byref(qpc))
            row = dict(qpc=qpc.value, wall_s=time.perf_counter()-start)
            before = word(pointers['frame_counter'])
            for name,(_,fmt) in fields.items():
                row[name] = struct.unpack('>'+fmt,read(vm+pointers[name],struct.calcsize(fmt)))[0]
            row['counter_stable'] = before == word(pointers['frame_counter'])
            rows.append(row)
            time.sleep(.02)
        frequency = c.c_longlong(); kernel.QueryPerformanceFrequency(c.byref(frequency))
        result = dict(pid=args.pid,exe=str(args.exe.resolve()),
                      exe_sha256=hashlib.sha256(args.exe.read_bytes()).hexdigest(),
                      qpc_frequency=frequency.value,base=hex(base),
                      pointers={k:hex(v) for k,v in pointers.items()},constants_hex=constants,
                      samples=rows,note=__doc__)
        args.output.write_text(json.dumps(result,indent=2)+'\n')
        print(json.dumps({k:v for k,v in result.items() if k != 'samples'},indent=2))
    finally:
        if symbols: dbg.SymCleanup(handle)
        kernel.CloseHandle(handle)


if __name__ == '__main__':
    main()
