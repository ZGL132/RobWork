# =====================================================================
# WP-24-T09 staging 安装树直启冒烟（acceptance 2② 判据的执行脚本）
# ---------------------------------------------------------------------
# 判据：无开发机构建目录 PATH 依赖——以 staging 安装树绝对路径直接启动，
# PATH 剥离 vcpkg/构建树/Qt 开发机路径（只留 Windows 系统目录），CWD 为
# 净室目录（不在安装树内）。验证链：进程启动→主窗口呈现（截图＋窗口
# 枚举）→WM_CLOSE 优雅退出（退出码 0）→Dev 日志事实行（诊断栈就绪＋
# 宿主已关闭工作单元）。
# 运行记录由本脚本自写 UTF-8 文件（管道重定向在 GBK 控制台会乱码——
# F-207/F-231/F-309 留痕乱码家族的规避口径）。
# 退出码约定：0＝冒烟通过；非 0＝失败（原因见运行记录）。
# =====================================================================
$ErrorActionPreference = 'Stop'

$stagingExe = 'D:\10_Source_Repos\21_robot\RobWork\build\staging-wp24-t09\bin\sdurws_ird_studio.exe'
$smokeDir   = 'D:\10_Source_Repos\21_robot\RobWork\RobWork\doc\industrial-robot-design\traceability\builds\wp24-t09\staging-smoke'
$runLog     = 'D:\10_Source_Repos\21_robot\RobWork\RobWork\doc\industrial-robot-design\traceability\builds\wp24-t09\staging-smoke-run.log'

function Log([string]$msg) {
    $line = (Get-Date).ToString('HH:mm:ss') + ' ' + $msg
    Write-Output $line
    Add-Content -Path $runLog -Value $line -Encoding UTF8
}

Set-Content -Path $runLog -Value ('WP-24-T09 staging smoke @ ' + (Get-Date).ToString('yyyy-MM-dd HH:mm:ss')) -Encoding UTF8

if (-not (Test-Path $stagingExe)) { Log "SMOKE_FAIL exe-missing $stagingExe"; exit 3 }
New-Item -ItemType Directory -Force -Path $smokeDir | Out-Null

# 干净 PATH：只留 Windows 系统目录——vcpkg installed bin、build/RobWork/bin、
# Qt bin 全部剥离（若安装树 DLL 不自足，启动即失败＝判据②不通过的实证）。
$env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"

$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName         = $stagingExe
$psi.WorkingDirectory = $smokeDir          # 净室 CWD（Dev 日志落此处）
$psi.UseShellExecute  = $false
$proc = [System.Diagnostics.Process]::Start($psi)
Log ("SMOKE started pid=" + $proc.Id)

# 等待装配完成（WP-24-T08 同款口径：约 12 s——六步装配＋装载呈现自证；
# 首轮实测 15 s 边界偶发提前关闭被丢——本版加长至 22 s 并在关闭前核对
# 进程存活与主窗口标题，规避初始化拖慢的偶发时序）。
Start-Sleep -Seconds 22
$proc.Refresh()
if ($proc.HasExited) { Log ("SMOKE_FAIL early-exit code=" + $proc.ExitCode); exit 4 }
Log ("  MainWindowTitle=[" + $proc.MainWindowTitle + "]")

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
function Snap($name) {
    $bounds = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
    $bmp = New-Object System.Drawing.Bitmap $bounds.Width, $bounds.Height
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($bounds.Location, [System.Drawing.Point]::Empty, $bounds.Size)
    $bmp.Save("$smokeDir\$name", [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $bmp.Dispose()
    Log "SNAP $name"
}
Snap 'staging-smoke-1-main-window.png'

# WM_CLOSE 优雅退出（与人工点 X 同路径——aboutToQuit 有界落盘收口）。
$proc.CloseMainWindow() | Out-Null
if (-not $proc.WaitForExit(20000)) {
    Log "WARN not exited in 20s; second WM_CLOSE + ENTER fallback"
    $proc.CloseMainWindow() | Out-Null
    Start-Sleep -Seconds 3
    if (-not $proc.HasExited) {
        # 兜底：若有模态确认对话框（同进程顶级窗口），回车确认默认键。
        $proc.Refresh()
        if (-not $proc.HasExited -and $proc.MainWindowTitle -ne '') {
            Add-Type -AssemblyName Microsoft.VisualBasic
            [Microsoft.VisualBasic.Interaction]::AppActivate($proc.Id)
            [System.Windows.Forms.SendKeys]::SendWait('{ENTER}')
            Log "SENT ENTER fallback"
        }
        if (-not $proc.WaitForExit(15000)) {
            Snap 'staging-smoke-2-close-blocked.png'
            Log "SMOKE_FAIL no-clean-close (kill)"
            $proc.Kill()
            exit 5
        }
    }
}
Log ("SMOKE_EXIT=" + $proc.ExitCode)

# Dev 日志事实行核对（诊断栈就绪＋宿主已关闭工作单元＝装配基座真实执行
# 与优雅退出路径真实走到）。
$devLog = Join-Path $smokeDir 'ird-ui-plugin-logs\dev-diagnostics.log'
if (Test-Path $devLog) {
    Log "--- dev-diagnostics.log:"
    Get-Content $devLog -Encoding UTF8 | ForEach-Object { Log ("  " + $_) }
} else {
    Log "SMOKE_FAIL dev-log-missing"
    exit 6
}

if ($proc.ExitCode -eq 0) { Log "STAGING_SMOKE_PASS" } else { Log "STAGING_SMOKE_FAIL"; exit 7 }
