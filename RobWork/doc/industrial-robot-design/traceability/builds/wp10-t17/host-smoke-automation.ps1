# =====================================================================
# host-smoke.ps1 —— UI-T17 宿主 GUI 冒烟自动化（操作序列留痕的执行体）
#
# 序列：启动宿主（ini 自动装载插件）→ File/子菜单/Tools/视图/Plugins 五处
# 菜单位置取证 → 新建项目协议 → 保存草稿链路 → 关闭项目确认流（取消路径
# ＋确认路径）→ 退出。每步截图，关键状态断言（退出码非 0＝存在失败步）。
# =====================================================================
$ErrorActionPreference = 'Continue'
$smoke   = 'C:/Users/zgl18/AppData/Local/Temp/wp10-t17-smoke'
$work    = "$smoke/work"
$shots   = "$smoke/shots"
$ini     = "$smoke/RobWorkStudio.ini"
$exe     = 'D:/10_Source_Repos/21_robot/RobWork/build/RobWorkStudio/bin/Release/RobWorkStudio.exe'
$devlog  = "$work/ird-ui-plugin-logs/dev-diagnostics.log"
New-Item -ItemType Directory -Force -Path $work, $shots | Out-Null

# PATH 前置（ui.md §13 v1.11 登记注④——S-3/F-320 装载环境清单）
$env:PATH = 'D:/10_Source_Repos/21_robot/RobWork/build/RobWorkStudio/bin/Release;' +
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
function Keys([string]$k, [int]$waitMs = 500) {
  [Microsoft.VisualBasic.Interaction]::AppActivate($global:proc.Id) | Out-Null
  Start-Sleep -Milliseconds 200
  [System.Windows.Forms.SendKeys]::SendWait($k)
  Start-Sleep -Milliseconds $waitMs
}
function DevLog-Contains([string]$needle) {
  if (-not (Test-Path $devlog)) { return $false }
  return (Select-String -Path $devlog -Pattern $needle -SimpleMatch -Quiet)
}
$fail = 0
function Assert-Log([string]$needle, [string]$step) {
  Start-Sleep -Milliseconds 800
  if (DevLog-Contains $needle) { Write-Output "PASS $step (devlog: $needle)" }
  else { Write-Output "FAIL $step (devlog 未含: $needle)"; $script:fail += 1 }
}

# ---- S0 启动（ini 自动装载插件）----
if (Test-Path $devlog) { Remove-Item $devlog -Force }
$global:proc = Start-Process -FilePath $exe -ArgumentList "--ini-file", $ini -WorkingDirectory $work -PassThru
Start-Sleep -Seconds 20
Shot 'host-smoke-01-startup'
Assert-Log '工作台装配完成' 'G1a 装载门控·装配完成'
Assert-Log '装载呈现自证完成' 'G1b 装载门控·Dock 已呈现'

# ---- S1 File 菜单位置（工业机器人项目 在 Reload 与 Preferences 之间）----
Keys '%f' 700
Shot 'host-smoke-02-file-menu'
Keys '{ESC}' 300

# ---- S2 子菜单内容（新建/打开/保存草稿/项目另存为/关闭项目/最近项目）----
Keys '%f' 700
Keys '{DOWN}{DOWN}{DOWN}{DOWN}{DOWN}{DOWN}' 500
Keys '{RIGHT}' 700
Shot 'host-smoke-03-project-submenu'
Keys '{ESC}' 200; Keys '{ESC}' 300

# ---- S3 Tools 菜单（命令面板入口）----
Keys '%t' 700
Shot 'host-smoke-04-tools-menu'
Keys '{ESC}' 300

# ---- S4 自建视图菜单（三区开关＋恢复默认布局；位于 Plugins 之前）----
Keys '%f' 700
Keys '{RIGHT}{RIGHT}' 500
Shot 'host-smoke-05-view-menu'
Keys '{RIGHT}' 500
Shot 'host-smoke-06-plugins-menu'
Keys '{ESC}' 300

# ---- S5 新建项目协议（目录对话框→显示名对话框→会话打开）----
$projDir = "$smoke/proj-t17"
New-Item -ItemType Directory -Force -Path $projDir | Out-Null
Keys '%f' 700
Keys '{DOWN}{DOWN}{DOWN}{DOWN}{DOWN}{DOWN}' 400
Keys '{RIGHT}' 600
Keys '{ENTER}' 1200
Shot 'host-smoke-07-newproj-dir-dialog'
Keys "$projDir" 300
Keys '{ENTER}' 2500
Shot 'host-smoke-08-newproj-name-dialog'
Keys '冒烟项目T17' 300
Keys '{ENTER}' 9000
Shot 'host-smoke-09-newproj-opened'
Assert-Log '项目已打开' 'G2 新建协议·项目已打开'

# ---- S6 保存草稿链路（draft.save 覆写处理器——saveAll(Manual) 真实链路）----
Keys '%f' 700
Keys '{DOWN}{DOWN}{DOWN}{DOWN}{DOWN}{DOWN}' 400
Keys '{RIGHT}' 600
Keys '{DOWN}{DOWN}' 400
Keys '{ENTER}' 2500
Shot 'host-smoke-10-draft-saved'

# ---- S7 关闭项目·取消路径（统一确认对话框呈现→Esc 回原状态）----
Keys '%f' 700
Keys '{DOWN}{DOWN}{DOWN}{DOWN}{DOWN}{DOWN}' 400
Keys '{RIGHT}' 600
Keys '{DOWN}{DOWN}{DOWN}{DOWN}' 400
Keys '{ENTER}' 1500
Shot 'host-smoke-11-close-confirm-dialog'
Keys '{ESC}' 1200
Shot 'host-smoke-12-after-cancel'

# ---- S8 关闭项目·确认路径（继续→Draining 轮询→项目已关闭）----
Keys '%f' 700
Keys '{DOWN}{DOWN}{DOWN}{DOWN}{DOWN}{DOWN}' 400
Keys '{RIGHT}' 600
Keys '{DOWN}{DOWN}{DOWN}{DOWN}' 400
Keys '{ENTER}' 1500
Keys '{ENTER}' 4000
Shot 'host-smoke-13-after-close-confirm'
Assert-Log 'draining entered' 'G3a 关闭流·Draining 进入'
Start-Sleep -Seconds 2
Shot 'host-smoke-14-final'

# ---- 收尾：正常退出宿主 ----
Keys '%{F4}' 1500
Start-Sleep -Seconds 3
if (-not $global:proc.HasExited) { Stop-Process -Id $global:proc.Id -Force }

Write-Output ("RESULT fail=" + $script:fail)
exit $script:fail
