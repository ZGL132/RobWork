Add-Type -TypeDefinition @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class RequirementDesktopRead {
 [DllImport("user32.dll")] public static extern IntPtr GetProcessWindowStation();
 [DllImport("user32.dll")] public static extern IntPtr GetThreadDesktop(uint tid);
 [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
 [DllImport("user32.dll",CharSet=CharSet.Unicode,SetLastError=true)] public static extern bool GetUserObjectInformation(IntPtr h,int index,StringBuilder text,int length,out int needed);
 [DllImport("user32.dll",SetLastError=true)] public static extern IntPtr OpenInputDesktop(uint flags,bool inherit,uint access);
 [DllImport("user32.dll")] public static extern bool CloseDesktop(IntPtr h);
 [DllImport("user32.dll",SetLastError=true)] public static extern bool OpenClipboard(IntPtr owner);
 [DllImport("user32.dll")] public static extern bool CloseClipboard();
 public static string Name(IntPtr h) { var s=new StringBuilder(512);int needed;return GetUserObjectInformation(h,2,s,1024,out needed)?s.ToString():"error="+Marshal.GetLastWin32Error(); }
}
"@
"SessionId=$((Get-Process -Id $PID).SessionId)"
"WindowStation=$([RequirementDesktopRead]::Name([RequirementDesktopRead]::GetProcessWindowStation()))"
"ThreadDesktop=$([RequirementDesktopRead]::Name([RequirementDesktopRead]::GetThreadDesktop([RequirementDesktopRead]::GetCurrentThreadId())))"
$reqInput=[RequirementDesktopRead]::OpenInputDesktop(0,$false,1)
"InputDesktopHandle=$reqInput Win32Error=$([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
if($reqInput -ne [IntPtr]::Zero) { "InputDesktop=$([RequirementDesktopRead]::Name($reqInput))"; [void][RequirementDesktopRead]::CloseDesktop($reqInput) }
$reqClipboard=[RequirementDesktopRead]::OpenClipboard([IntPtr]::Zero)
"OpenClipboard=$reqClipboard Win32Error=$([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
if($reqClipboard) { [void][RequirementDesktopRead]::CloseClipboard() }

Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class ReqJobAudit {
 [DllImport("kernel32.dll", SetLastError=true)] public static extern bool QueryInformationJobObject(IntPtr job,int kind,out UInt32 info,UInt32 length,out UInt32 returned);
 [DllImport("kernel32.dll", SetLastError=true)] public static extern bool IsProcessInJob(IntPtr process,IntPtr job,out bool inJob);
 [DllImport("kernel32.dll")] public static extern IntPtr GetCurrentProcess();
}
"@
$reqInJob=$false
$reqQuery=[ReqJobAudit]::IsProcessInJob([ReqJobAudit]::GetCurrentProcess(),[IntPtr]::Zero,[ref]$reqInJob)
"IsProcessInJobQuery=$reqQuery InJob=$reqInJob Error=$([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
[uint32]$reqUiLimits=0
[uint32]$reqReturned=0
$reqQuery=[ReqJobAudit]::QueryInformationJobObject([IntPtr]::Zero,4,[ref]$reqUiLimits,4,[ref]$reqReturned)
"QueryBasicUiRestrictions=$reqQuery Flags=$reqUiLimits Error=$([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
"ReadClipboardRestricted=$(($reqUiLimits -band 2) -ne 0) WriteClipboardRestricted=$(($reqUiLimits -band 4) -ne 0)"