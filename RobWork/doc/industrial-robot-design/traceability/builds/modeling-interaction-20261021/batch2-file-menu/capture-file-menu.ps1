$ErrorActionPreference='Stop'
$taskRoot='C:/Users/zgl18/.codex/worktrees/486b/RobWork'
$taskOut="$taskRoot/build/modeling-interaction-20261021/batch2-menu-shots"
New-Item -ItemType Directory -Force -Path $taskOut | Out-Null
# 当前工具集无node_repl/sky；用Windows菜单助记消息和屏幕截图观察真实程序。
# 仅本次新启动进程；不写项目、不结束用户进程。
& "$taskRoot/RobWork/doc/industrial-robot-design/traceability/builds/modeling-interaction-20261021/Start-Manual.ps1"
Start-Sleep -Seconds 4
$taskApp=@(Get-Process sdurws_ird_studio | Where-Object {$_.Path.Replace('\','/') -eq "$taskRoot/build/RobWorkStudio/bin/Release/sdurws_ird_studio.exe"})
if($taskApp.Count -ne 1){throw '正式窗口无法唯一定位'}
$p=$taskApp[0];$p.Refresh();$hwnd=$p.MainWindowHandle
Add-Type -AssemblyName System.Windows.Forms,System.Drawing,UIAutomationClient,UIAutomationTypes
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class MenuWindowNative {
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")] public static extern void mouse_event(uint flags,uint dx,uint dy,uint data,UIntPtr extra);
}
'@
[MenuWindowNative]::SetForegroundWindow($hwnd) | Out-Null
# 先从实际可访问树定位唯一File，再按它当前屏幕矩形点击，不猜坐标。
$root=[Windows.Automation.AutomationElement]::FromHandle($hwnd)
$files=$root.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty,'File'))
if($files.Count -ne 1){throw 'File菜单无法唯一定位'}
$menuBounds=$files[0].Current.BoundingRectangle
[MenuWindowNative]::SetCursorPos([int]($menuBounds.X+$menuBounds.Width/2),[int]($menuBounds.Y+$menuBounds.Height/2)) | Out-Null
[MenuWindowNative]::mouse_event(2,0,0,0,[UIntPtr]::Zero)
[MenuWindowNative]::mouse_event(4,0,0,0,[UIntPtr]::Zero)
Start-Sleep -Milliseconds 700
$r=$root.Current.BoundingRectangle
$bounds=[Drawing.Rectangle]::new([int]$r.X,[int]$r.Y,[int]$r.Width,[int]$r.Height)
$bitmap=[Drawing.Bitmap]::new($bounds.Width,$bounds.Height)
$graphics=[Drawing.Graphics]::FromImage($bitmap)
$graphics.CopyFromScreen($bounds.X,$bounds.Y,0,0,$bounds.Size)
$bitmap.Save("$taskOut/file-menu.png",[Drawing.Imaging.ImageFormat]::Png)
$graphics.Dispose();$bitmap.Dispose()
$root=[Windows.Automation.AutomationElement]::FromHandle($hwnd)
$items=$root.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty,[Windows.Automation.ControlType]::MenuItem))
@($items | ForEach-Object { [pscustomobject]@{Name=$_.Current.Name;Enabled=$_.Current.IsEnabled;Offscreen=$_.Current.IsOffscreen} }) | ConvertTo-Json | Set-Content "$taskOut/menu-accessibility.json" -Encoding utf8
[Windows.Forms.SendKeys]::SendWait('{ESC}')
Start-Sleep -Milliseconds 200
$p.CloseMainWindow() | Out-Null
if(!$p.WaitForExit(10000)){throw '本次窗口未退出，请检查模态对话框；未强杀进程'}
Write-Output "formal menu screenshot saved; process=$($p.Id); exit=$($p.ExitCode)"
