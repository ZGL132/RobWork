import ctypes as c, struct, json
from ctypes import wintypes as w
k=c.WinDLL('kernel32',use_last_error=True); d=c.WinDLL('dbghelp',use_last_error=True)
k.OpenProcess.argtypes=[w.DWORD,w.BOOL,w.DWORD];k.OpenProcess.restype=w.HANDLE
k.OpenThread.argtypes=[w.DWORD,w.BOOL,w.DWORD];k.OpenThread.restype=w.HANDLE
for n in ['SuspendThread','ResumeThread','GetThreadContext','CloseHandle']: getattr(k,n).argtypes=[w.HANDLE] if n!='GetThreadContext' else [w.HANDLE,c.c_void_p]
d.SymInitializeW.argtypes=[w.HANDLE,w.LPCWSTR,w.BOOL]; d.SymInitializeW.restype=w.BOOL
d.SymGetModuleBase64.argtypes=[w.HANDLE,c.c_uint64]; d.SymGetModuleBase64.restype=c.c_uint64
d.SymFromAddr.argtypes=[w.HANDLE,c.c_uint64,c.POINTER(c.c_uint64),c.c_void_p]; d.SymFromAddr.restype=w.BOOL
d.StackWalk64.argtypes=[w.DWORD,w.HANDLE,w.HANDLE,c.c_void_p,c.c_void_p,c.c_void_p,c.c_void_p,c.c_void_p,c.c_void_p];d.StackWalk64.restype=w.BOOL
p=k.OpenProcess(0x1f0fff,False,74616); t=k.OpenThread(0x1f03ff,False,74920)
assert p and t, c.get_last_error()
d.SymSetOptions(0x10|0x2|0x4|0x200)
assert d.SymInitializeW(p,'C:/Users/zgl18/.codex/worktrees/486b/RobWork/build/RobWorkStudio/bin/Release;D:/software/Qt/6.11.1/msvc2022_64/bin',True), c.get_last_error()
ctx=c.create_string_buffer(1232+16);ctxaddr=(c.addressof(ctx)+15)&~15
c.c_uint32.from_address(ctxaddr+48).value=0x10000b
stack=c.create_string_buffer(4096)
frames=[]
k.SuspendThread(t)
try:
    assert k.GetThreadContext(t,ctxaddr), c.get_last_error()
    for off,reg in [(0,248),(32,160),(48,152)]:
        c.c_uint64.from_buffer(stack,off).value=c.c_uint64.from_address(ctxaddr+reg).value
        c.c_uint32.from_buffer(stack,off+12).value=3
    for i in range(45):
        ok=d.StackWalk64(0x8664,p,t,stack,ctxaddr,None,c.cast(d.SymFunctionTableAccess64,c.c_void_p),c.cast(d.SymGetModuleBase64,c.c_void_p),None)
        if not ok:break
        addr=c.c_uint64.from_buffer(stack,0).value
        if not addr:break
        sym=c.create_string_buffer(2048);c.c_uint32.from_buffer(sym,0).value=88;c.c_uint32.from_buffer(sym,80).value=1900
        delta=c.c_uint64()
        found=d.SymFromAddr(p,addr,c.byref(delta),sym)
        name=sym.raw[84:].split(b'\x00')[0].decode('utf-8','replace') if found else ''
        frames.append({'address':hex(addr),'symbol':name,'offset':delta.value})
finally:
    k.ResumeThread(t);k.CloseHandle(t);k.CloseHandle(p)
print(json.dumps(frames,indent=2))
