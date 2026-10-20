# Windows 调试事件取证：真实宿主仍用绝对路径、VS x64与windows平台，仅一实例。
import ctypes as C, struct, subprocess, pathlib, sys
from ctypes import wintypes as W
sys.stdout.reconfigure(encoding='utf-8')
k=C.WinDLL('kernel32',use_last_error=True)
k.WaitForDebugEvent.argtypes=[C.c_void_p,W.DWORD]
k.ContinueDebugEvent.argtypes=[W.DWORD,W.DWORD,W.DWORD]
k.OpenThread.argtypes=[W.DWORD,W.BOOL,W.DWORD];k.OpenThread.restype=W.HANDLE
k.GetThreadContext.argtypes=[W.HANDLE,C.c_void_p]
k.ReadProcessMemory.argtypes=[W.HANDLE,C.c_void_p,C.c_void_p,C.c_size_t,C.c_void_p]
k.CloseHandle.argtypes=[W.HANDLE]
exe=pathlib.Path(sys.argv[1]).resolve();out=pathlib.Path(sys.argv[2]).resolve();out.mkdir(exist_ok=True)
proc=subprocess.Popen([str(exe)],cwd=exe.parent,stdout=(out/'stdout.log').open('wb'),stderr=(out/'stderr.log').open('wb'),creationflags=0x08000002)
base=0
while True:
 e=C.create_string_buffer(176)
 if not k.WaitForDebugEvent(e,1000):
  continue
 code,pid,tid=struct.unpack_from('<III',e.raw)
 cont=0x00010002
 if code==3:
  base=struct.unpack_from('<Q',e.raw,40)[0]
  print('exe_base',hex(base),flush=True)
 if code==1:
  ec=struct.unpack_from('<I',e.raw,16)[0];address=struct.unpack_from('<Q',e.raw,32)[0];chance=struct.unpack_from('<I',e.raw,168)[0]
  if ec==0xc0000005:
   print('access_violation',hex(address),'rva',hex(address-base),'first_chance',chance,flush=True)
   thread=k.OpenThread(0x001a,False,tid)
   context=C.create_string_buffer(1248);struct.pack_into('<I',context,48,0x0010001f)
   if k.GetThreadContext(thread,context):
    rsp=struct.unpack_from('<Q',context.raw,152)[0]
    memory=C.create_string_buffer(2048);size=C.c_size_t()
    if k.ReadProcessMemory(int(proc._handle),C.c_void_p(rsp),memory,2048,C.byref(size)):
     for i in range(0,size.value,8):
      value=struct.unpack_from('<Q',memory.raw,i)[0]
      if base<=value<base+0x1600000:
       print('stack',hex(i),'exe_rva',hex(value-base),flush=True)
   k.CloseHandle(thread)
  if ec!=0x80000003:cont=0x80010001
 k.ContinueDebugEvent(pid,tid,cont)
 if code==5:
  print('exit_event',struct.unpack_from('<I',e.raw,16)[0],flush=True)
  break
sys.exit(proc.wait())
