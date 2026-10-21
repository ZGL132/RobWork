$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Windows.Forms,System.Drawing,UIAutomationClient,UIAutomationTypes
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class TreeWindowNative {
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")] public static extern void mouse_event(uint flags,uint dx,uint dy,uint data,UIntPtr extra);
}
"@
$taskPath='C:/Users/zgl18/.codex/worktrees/486b/RobWork'
$taskApp=@(Get-Process sdurws_ird_studio | Where-Object {$_.Path.Replace('\','/') -eq "$taskPath/build/RobWorkStudio/bin/Release/sdurws_ird_studio.exe"})
if($taskApp.Count -ne 1){throw '本树正式窗口无法唯一定位'}
$taskApp=$taskApp[0]
$taskTop=[Windows.Automation.AutomationElement]::RootElement.FindAll([Windows.Automation.TreeScope]::Children,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ProcessIdProperty,$taskApp.Id))
$taskMain=@($taskTop | Where-Object {$_.Current.Name -match 'RobWorkStudio v'})
if($taskMain.Count -ne 1){throw '正式主窗口无法唯一定位'}
$taskHwnd=[IntPtr]$taskMain[0].Current.NativeWindowHandle
[TreeWindowNative]::SetForegroundWindow($taskHwnd)|Out-Null
function UiRoot { [Windows.Automation.AutomationElement]::FromHandle($taskHwnd) }
function Named([string]$name){
 $matches=(UiRoot).FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty,$name))
 if($matches.Count -ne 1){throw "$name 定位数=$($matches.Count)"}
 return $matches[0]
}
function ClickElement($element){
 $r=$element.Current.BoundingRectangle
 if($element.Current.IsOffscreen -or $r.Width -le 0){throw '目标未显示，不猜坐标'}
 [TreeWindowNative]::SetCursorPos([int]($r.X+$r.Width/2),[int]($r.Y+$r.Height/2))|Out-Null
 [TreeWindowNative]::mouse_event(2,0,0,0,[UIntPtr]::Zero)
 [TreeWindowNative]::mouse_event(4,0,0,0,[UIntPtr]::Zero)
 Start-Sleep -Milliseconds 400
}
function Observe([string]$label){
 $items=(UiRoot).FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.Condition]::TrueCondition)
 @($items|ForEach-Object{[pscustomobject]@{Name=$_.Current.Name;Id=$_.Current.AutomationId;Type=$_.Current.ControlType.ProgrammaticName;Offscreen=$_.Current.IsOffscreen}})|ConvertTo-Json -Depth 3|Set-Content "$taskPath/build/modeling-interaction-20261021/batch3-ui-$label.json" -Encoding utf8
 @($items|Where-Object {!$_.Current.IsOffscreen -and $_.Current.Name}|ForEach-Object{$_.Current.ControlType.ProgrammaticName+' '+$_.Current.Name})
}
function Screenshot([string]$label){
 $r=(UiRoot).Current.BoundingRectangle
 $b=[Drawing.Rectangle]::new([int]$r.X,[int]$r.Y,[int]$r.Width,[int]$r.Height)
 $img=[Drawing.Bitmap]::new($b.Width,$b.Height)
 $g=[Drawing.Graphics]::FromImage($img)
 $g.CopyFromScreen($b.X,$b.Y,0,0,$b.Size)
 $img.Save("$taskPath/build/modeling-interaction-20261021/batch3-host-$label.png",[Drawing.Imaging.ImageFormat]::Png)
 $g.Dispose();$img.Dispose()
}
