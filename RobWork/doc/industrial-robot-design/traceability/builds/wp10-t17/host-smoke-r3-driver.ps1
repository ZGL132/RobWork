# =====================================================================
# host-smoke-r3-driver.ps1 —— UI-T17 返工（attempt 2）宿主 GUI 冒烟驱动
#
# 与 attempt 1 盲驱 SendKeys 脚本的本质区别（B-1 返工输入③⑤的落地面）：
#   1. 原子动作制——每次调用只做一个动作（launch/shot/keys/click/act/quit），
#      截图由实施者用视觉审读后再决定下一步坐标/键序（闭环，不盲驱）；
#   2. 原生对话框不再依赖键盘 Enter（Enter＝进入文件夹不是确认）——目标
#      是鼠标点击『选择文件夹』按钮（坐标来自截图审读）；
#   3. 判据以逐帧截图＋状态行为主，dev 日志只作进程退出后的辅证（F-325：
#      dev 日志存活期采样尾段不可靠）。
# =====================================================================
param(
  [Parameter(Mandatory = $true)][string]$Action,
  [string]$Name = 'shot',
  [string]$KeysText = '',
  [int]$X = 0,
  [int]$Y = 0,
  [int]$WaitMs = 500
)

$ErrorActionPreference = 'Continue'
# 冒烟现场（与 attempt 1 现场分离——r3 专属目录，避免留痕混淆）
$smoke = 'C:/Users/zgl18/AppData/Local/Temp/wp10-t17-smoke-r3'
$work  = "$smoke/work"
$shots = "$smoke/shots"
$pidFile = "$smoke/host.pid"
$exe   = 'D:/10_Source_Repos/21_robot/RobWork/build/RobWorkStudio/bin/Release/RobWorkStudio.exe'
$plugin = 'D:/10_Source_Repos/21_robot/RobWork/build/RobWorkStudio/libs/Release/sdurws_ird_ui_plugin.dll'
$devlog = "$work/ird-ui-plugin-logs/dev-diagnostics.log"
New-Item -ItemType Directory -Force -Path $work, $shots | Out-Null

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName Microsoft.VisualBasic
# user32 鼠标注入（SetCursorPos＋mouse_event——原生对话框按钮点击的承载面，
# B-1 返工输入③"鼠标点击选择文件夹"的执行体）
if (-not ('IrdSmokeNative' -as [type])) {
  Add-Type -Namespace IrdSmoke -Name Native -MemberDefinition @"
[DllImport("user32.dll")] public static extern bool SetCursorPos(int X, int Y);
[DllImport("user32.dll")] public static extern void mouse_event(uint dwFlags, uint dx, uint dy, uint dwData, UIntPtr dwExtraInfo);
"@
}

function Get-HostProcess {
  if (-not (Test-Path $pidFile)) { return $null }
  $id = Get-Content $pidFile -ErrorAction SilentlyContinue
  if (-not $id) { return $null }
  return Get-Process -Id $id -ErrorAction SilentlyContinue
}

function Shot([string]$name) {
  Start-Sleep -Milliseconds 400
  $b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
  $bmp = New-Object System.Drawing.Bitmap $b.Width, $b.Height
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($b.Location, [System.Drawing.Point]::Empty, $b.Size)
  $bmp.Save("$shots/$name.png", [System.Drawing.Imaging.ImageFormat]::Png)
  $g.Dispose(); $bmp.Dispose()
  Write-Output "SHOT $shots/$name.png ($($b.Width)x$($b.Height))"
}

switch ($Action) {

  # ---- 启动宿主：--rwsplugin 直载通道（F-320 首通道；attempt 1 实测可用；
  #      放弃 --ini-file 通道——F-324）。PATH 前置＝HANDOVER 清单＋验收增补。
  'launch' {
    if (Test-Path $devlog) { Remove-Item $devlog -Force }
    $env:PATH = 'D:/10_Source_Repos/21_robot/RobWork/build/RobWorkStudio/bin/Release;' +
                'D:/10_Source_Repos/21_robot/RobWork/build/RobWork/bin/Release;' +
                'D:/10_Source_Repos/21_robot/RobWork/vcpkg/installed/x64-windows/bin;' +
                'D:/software/Qt/6.11.1/msvc2022_64/bin;D:/software/Miniconda3;' + $env:PATH
    $env:QT_QPA_PLATFORM = 'windows'
    $p = Start-Process -FilePath $exe -ArgumentList '--rwsplugin', $plugin -WorkingDirectory $work -PassThru
    Set-Content -Path $pidFile -Value $p.Id
    Write-Output "LAUNCH pid=$($p.Id)"
  }

  # ---- 全屏截图（视觉审读用）
  'shot' { Shot $Name }

  # ---- 激活宿主窗口（前台焦点——SendKeys 前置）
  'act' {
    $p = Get-HostProcess
    if ($null -eq $p) { Write-Output 'ACT-FAIL no-host'; exit 1 }
    [Microsoft.VisualBasic.Interaction]::AppActivate($p.Id) | Out-Null
    Start-Sleep -Milliseconds 200
    Write-Output "ACT pid=$($p.Id) title=$($p.MainWindowTitle)"
  }

  # ---- 键序注入（先激活再发送；等待后返回）
  'keys' {
    $p = Get-HostProcess
    if ($null -eq $p) { Write-Output 'KEYS-FAIL no-host'; exit 1 }
    [Microsoft.VisualBasic.Interaction]::AppActivate($p.Id) | Out-Null
    Start-Sleep -Milliseconds 150
    [System.Windows.Forms.SendKeys]::SendWait($KeysText)
    Start-Sleep -Milliseconds $WaitMs
    Write-Output "KEYS '$KeysText'"
  }

  # ---- 鼠标左键单击（绝对屏幕坐标——截图审读后给出）
  'click' {
    [IrdSmoke.Native]::SetCursorPos($X, $Y) | Out-Null
    Start-Sleep -Milliseconds 120
    [IrdSmoke.Native]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)  # LEFTDOWN
    [IrdSmoke.Native]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)  # LEFTUP
    Start-Sleep -Milliseconds $WaitMs
    Write-Output "CLICK $X,$Y"
  }

  # ---- 收尾：WM_CLOSE 优雅退出→超时强杀（退出后 dev 日志采样尾段落盘）
  'quit' {
    $p = Get-HostProcess
    if ($null -eq $p) { Write-Output 'QUIT no-host'; exit 0 }
    $p.CloseMainWindow() | Out-Null
    Start-Sleep -Seconds 6
    $p.Refresh()
    if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force; Write-Output 'QUIT forced-kill' }
    else { Write-Output 'QUIT graceful' }
  }

  default { Write-Output "UNKNOWN action $Action"; exit 2 }
}
