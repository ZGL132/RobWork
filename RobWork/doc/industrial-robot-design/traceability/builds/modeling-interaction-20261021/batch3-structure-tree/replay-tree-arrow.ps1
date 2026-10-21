$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class TreeReplayNative {
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")] public static extern void mouse_event(uint flags,uint dx,uint dy,uint data,UIntPtr extra);
}
"@
$t=Get-Content build/modeling-interaction-20261021/batch3-host-replay-target.json|ConvertFrom-Json
$p=Get-Process -Id $t.pid
[TreeReplayNative]::SetForegroundWindow([IntPtr]$t.hwnd)|Out-Null
Start-Sleep -Milliseconds 500
# 使用前一观测实际返回的箭头位置；此后不枚举UIA树，隔离Windows提供者的超时。
[TreeReplayNative]::SetCursorPos($t.X,$t.Y)|Out-Null
[TreeReplayNative]::mouse_event(2,0,0,0,[UIntPtr]::Zero)
[TreeReplayNative]::mouse_event(4,0,0,0,[UIntPtr]::Zero)
Start-Sleep -Seconds 2
$p.Refresh()
$status=[pscustomobject]@{pid=$p.Id;responding=$p.Responding;cpu=$p.CPU;x=$t.X;y=$t.Y;uiaEnumerationAfterClick=$false}
$status|ConvertTo-Json|Set-Content build/modeling-interaction-20261021/batch3-host-arrow-replay.json
$b=[Drawing.Rectangle]::new($t.WinX,$t.WinY,$t.WinWidth,$t.WinHeight)
$img=[Drawing.Bitmap]::new($b.Width,$b.Height);$g=[Drawing.Graphics]::FromImage($img)
$g.CopyFromScreen($b.X,$b.Y,0,0,$b.Size)
$img.Save('C:/Users/zgl18/.codex/worktrees/486b/RobWork/build/modeling-interaction-20261021/batch3-host-arrow-replay.png',[Drawing.Imaging.ImageFormat]::Png)
$g.Dispose();$img.Dispose()
$status|ConvertTo-Json
$p.CloseMainWindow()|Out-Null
if(!$p.WaitForExit(10000)){throw '本次窗口未退出，保留进程检查'}
