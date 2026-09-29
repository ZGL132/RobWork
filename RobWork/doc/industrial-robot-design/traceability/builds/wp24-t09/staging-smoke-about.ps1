# =====================================================================
# WP-24-T09 staging 安装树 About 交互冒烟（acceptance 2⑦ 判据执行脚本）
# ---------------------------------------------------------------------
# 判据：从安装树启动并核对 About 清单与静态装配内容——命令面板
# （Ctrl+Shift+P）过滤 "about" 执行 help.about，截图关于对话框（插件
# 清单表＝白名单∩装配报告现取：建模/需求/运动学三域"已装配"＋其余
# 白名单占位"未装配"如实呈现）。对话框 Esc 关闭后 WM_CLOSE 优雅退出。
# 输入法注记：系统中文输入法会劫持逐字符键入（WP-24-T08 实录）——
# 发送前先按 Shift 切英文模式，再整串发送 "about"。
# 运行记录自写 UTF-8 文件（GBK 控制台管道乱码规避）。
# =====================================================================
$ErrorActionPreference = 'Stop'

$stagingExe = 'D:\10_Source_Repos\21_robot\RobWork\build\staging-wp24-t09\bin\sdurws_ird_studio.exe'
$smokeDir   = 'D:\10_Source_Repos\21_robot\RobWork\RobWork\doc\industrial-robot-design\traceability\builds\wp24-t09\staging-smoke'
$runLog     = 'D:\10_Source_Repos\21_robot\RobWork\RobWork\doc\industrial-robot-design\traceability\builds\wp24-t09\staging-smoke-about-run.log'

function Log([string]$msg) {
    $line = (Get-Date).ToString('HH:mm:ss') + ' ' + $msg
    Write-Output $line
    Add-Content -Path $runLog -Value $line -Encoding UTF8
}
Set-Content -Path $runLog -Value ('WP-24-T09 staging about-smoke @ ' + (Get-Date).ToString('yyyy-MM-dd HH:mm:ss')) -Encoding UTF8

if (-not (Test-Path $stagingExe)) { Log "SMOKE_FAIL exe-missing"; exit 3 }
New-Item -ItemType Directory -Force -Path $smokeDir | Out-Null
$env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"

$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName         = $stagingExe
$psi.WorkingDirectory = $smokeDir
$psi.UseShellExecute  = $false
$proc = [System.Diagnostics.Process]::Start($psi)
Log ("SMOKE started pid=" + $proc.Id)
Start-Sleep -Seconds 22
$proc.Refresh()
if ($proc.HasExited) { Log ("SMOKE_FAIL early-exit code=" + $proc.ExitCode); exit 4 }

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName Microsoft.VisualBasic
function Snap($name) {
    $bounds = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
    $bmp = New-Object System.Drawing.Bitmap $bounds.Width, $bounds.Height
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($bounds.Location, [System.Drawing.Point]::Empty, $bounds.Size)
    $bmp.Save("$smokeDir\$name", [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $bmp.Dispose()
    Log "SNAP $name"
}

# 前台化主窗口后开命令面板：Ctrl+Shift+P→Shift 切英文→整串 "about"→Enter。
# 逐步日志：SendKeys 交互不可靠（焦点漂移/输入法劫持/首跑偶发阻塞——
# 首轮实录 16:08:35 palette 截图后无后续），每步落日志便于定位卡点。
try { [Microsoft.VisualBasic.Interaction]::AppActivate($proc.Id); Log "ACTIVATE ok" }
catch { Log ("ACTIVATE fail: " + $_.Exception.Message) }
Start-Sleep -Milliseconds 800
try { [System.Windows.Forms.SendKeys]::SendWait('^+p'); Log "SENT ctrl+shift+p" }
catch { Log ("SEND ctrl+shift+p fail: " + $_.Exception.Message) }
Start-Sleep -Milliseconds 1500
try { [System.Windows.Forms.SendKeys]::SendWait('+'); Log "SENT shift(en)" }
catch { Log ("SEND shift fail: " + $_.Exception.Message) }
Start-Sleep -Milliseconds 500
try { [System.Windows.Forms.SendKeys]::SendWait('about'); Log "SENT about" }
catch { Log ("SEND about fail: " + $_.Exception.Message) }
Start-Sleep -Milliseconds 1500
Snap 'staging-smoke-3-palette-about.png'
try { [System.Windows.Forms.SendKeys]::SendWait('{ENTER}'); Log "SENT enter" }
catch { Log ("SEND enter fail: " + $_.Exception.Message) }
Start-Sleep -Seconds 3
Snap 'staging-smoke-4-about-dialog.png'
# Esc 关闭关于对话框（模态）→WM_CLOSE 优雅退出。
try { [System.Windows.Forms.SendKeys]::SendWait('{ESC}'); Log "SENT esc" }
catch { Log ("SEND esc fail: " + $_.Exception.Message) }
Start-Sleep -Milliseconds 800
$proc.CloseMainWindow() | Out-Null
if (-not $proc.WaitForExit(20000)) {
    Snap 'staging-smoke-5-close-blocked.png'
    Log "WARN first WM_CLOSE not exited; retry + ENTER fallback"
    $proc.CloseMainWindow() | Out-Null
    Start-Sleep -Seconds 3
    if (-not $proc.HasExited) {
        $proc.Refresh()
        if (-not $proc.HasExited -and $proc.MainWindowTitle -ne '') {
            try { [Microsoft.VisualBasic.Interaction]::AppActivate($proc.Id) } catch {}
            [System.Windows.Forms.SendKeys]::SendWait('{ENTER}')
            Log "SENT ENTER fallback"
        }
        if (-not $proc.WaitForExit(15000)) {
            Snap 'staging-smoke-6-close-blocked2.png'
            Log "SMOKE_FAIL no-clean-close (kill)"
            $proc.Kill(); exit 5
        }
    }
}
Log ("SMOKE_EXIT=" + $proc.ExitCode)
if ($proc.ExitCode -eq 0) { Log "STAGING_ABOUT_SMOKE_PASS" } else { Log "STAGING_ABOUT_SMOKE_FAIL"; exit 7 }
