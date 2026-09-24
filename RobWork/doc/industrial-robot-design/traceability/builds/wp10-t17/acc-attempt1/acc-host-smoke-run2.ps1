# acc-host-smoke-run2.ps1 —— UI-T17 验收段宿主冒烟（外科手术式第 2 版）
# 变化点：①新建项目＝点击主页"新建项目"按钮（坐标全帧稳定 464,231）；
# ②保存/关闭＝Alt+F 菜单键盘导航（每键 300ms 间隔，无中途 ESC）；
# ③退出＝Alt+F4 优雅关闭（保住 dev 日志缓冲），3s 后强杀兜底。
$ErrorActionPreference = 'Continue'
$a      = 'D:/10_Source_Repos/21_robot/acc-tmp/ui-t17/a'
$smoke  = 'D:/10_Source_Repos/21_robot/acc-tmp/ui-t17/acc-smoke-run'
$work   = "$smoke/work"
$shots  = "$smoke/shots-r2"
$exe    = "$a/build/RobWorkStudio/bin/Release/RobWorkStudio.exe"
$devlog = "$work/ird-ui-plugin-logs/dev-diagnostics.log"
New-Item -ItemType Directory -Force -Path $work, $shots | Out-Null
$env:PATH = "$a/build/RobWorkStudio/bin/Release;$a/build/RobWork/bin/Release;" +
            'D:/10_Source_Repos/21_robot/RobWork/vcpkg/installed/x64-windows/bin;' +
            'D:/software/Qt/6.11.1/msvc2022_64/bin;D:/software/Miniconda3;' + $env:PATH
$env:QT_QPA_PLATFORM = 'windows'
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName Microsoft.VisualBasic

function Shot([string]$name) {
  Start-Sleep -Milliseconds 600
  $b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
  $bmp = New-Object System.Drawing.Bitmap $b.Width, $b.Height
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($b.Location, [System.Drawing.Point]::Empty, $b.Size)
  $bmp.Save("$shots/$name.png", [System.Drawing.Imaging.ImageFormat]::Png)
  $g.Dispose(); $bmp.Dispose()
  Write-Output "SHOT $name"
}
function Keys([string]$k, [int]$waitMs = 400) {
  [Microsoft.VisualBasic.Interaction]::AppActivate($global:proc.Id) | Out-Null
  Start-Sleep -Milliseconds 200
  [System.Windows.Forms.SendKeys]::SendWait($k)
  Start-Sleep -Milliseconds $waitMs
}
function KeysSlow([string[]]$ks, [int]$waitMs = 350) {
  foreach ($k in $ks) { Keys $k $waitMs }
}
if (-not ('M' -as [type])) {
  Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class M {
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint x, uint y, uint d, UIntPtr e);
}
'@
}
function Click([int]$x, [int]$y) {
  [Microsoft.VisualBasic.Interaction]::AppActivate($global:proc.Id) | Out-Null
  Start-Sleep -Milliseconds 200
  [System.Windows.Forms.Cursor]::Position = New-Object System.Drawing.Point($x, $y)
  Start-Sleep -Milliseconds 150
  [M]::mouse_event(2, 0, 0, 0, [UIntPtr]::Zero)  # LEFTDOWN
  [M]::mouse_event(4, 0, 0, 0, [UIntPtr]::Zero)  # LEFTUP
  Start-Sleep -Milliseconds 400
}

if (Test-Path $devlog) { Remove-Item $devlog -Force }
$global:proc = Start-Process -FilePath $exe -ArgumentList "-nosplash", "--rwsplugin", "$a/build/RobWorkStudio/libs/Release/sdurws_ird_ui_plugin.dll" -WorkingDirectory $work -PassThru
Start-Sleep -Seconds 20
Shot 'r2-01-startup'

# ---- S5 新建项目：点击主页"新建项目"按钮 ----
Click 464 231
Shot 'r2-02-dir-dialog'
$projDir = "$smoke/work/proj-t17"
New-Item -ItemType Directory -Force -Path $projDir | Out-Null
Keys "proj-t17" 400
Click 1732 1141
Shot 'r2-03-name-dialog'
Keys '冒烟项目T17' 300
Keys '{ENTER}' 9000
Shot 'r2-04-opened'

# ---- S6 保存草稿：File→工业机器人项目→保存草稿 ----
Keys '%f' 800
KeysSlow @('{DOWN}','{DOWN}','{DOWN}','{DOWN}','{DOWN}','{DOWN}') 300
Keys '{RIGHT}' 700
Shot 'r2-05-submenu-save'
KeysSlow @('{DOWN}','{DOWN}') 300
Keys '{ENTER}' 2500
Shot 'r2-06-draft-saved'

# ---- S7 关闭·取消路径：File→工业机器人项目→关闭项目→Esc ----
Keys '%f' 800
KeysSlow @('{DOWN}','{DOWN}','{DOWN}','{DOWN}','{DOWN}','{DOWN}') 300
Keys '{RIGHT}' 700
KeysSlow @('{DOWN}','{DOWN}','{DOWN}','{DOWN}') 300
Keys '{ENTER}' 1500
Shot 'r2-07-close-dialog'
Keys '{ESC}' 1200
Shot 'r2-08-after-cancel'

# ---- S8 关闭·确认路径：继续→Draining→项目已关闭 ----
Keys '%f' 800
KeysSlow @('{DOWN}','{DOWN}','{DOWN}','{DOWN}','{DOWN}','{DOWN}') 300
Keys '{RIGHT}' 700
KeysSlow @('{DOWN}','{DOWN}','{DOWN}','{DOWN}') 300
Keys '{ENTER}' 1500
Keys '{ENTER}' 4000
Shot 'r2-09-after-close-confirm'
Start-Sleep -Seconds 2
Shot 'r2-10-final'

# ---- 收尾：优雅退出 ----
Keys '%{F4}' 1500
Start-Sleep -Seconds 4
if (-not $global:proc.HasExited) { Stop-Process -Id $global:proc.Id -Force }
Start-Sleep -Seconds 1
Write-Output '---- devlog tail ----'
if (Test-Path $devlog) { Get-Content $devlog -Encoding UTF8 | ForEach-Object { Write-Output $_ } } else { Write-Output 'DEVLOG MISSING' }
