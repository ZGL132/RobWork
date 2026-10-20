# WP-14-T12：Windows正式宿主复现入口；VS x64、windows平台、一次一个绝对exe。
param([string]$Smoke = '', [Parameter(Mandatory=$true)][string]$OutputDir, [switch]$Wait, [int]$TimeoutSeconds = 180)
$ErrorActionPreference = 'Stop'
if (Get-Process sdurws_ird_studio -ErrorAction SilentlyContinue) { throw '已有正式宿主进程，禁止并发GUI' }
Import-Module 'D:/software/Microsoft Visual Studio/2022/Community/Common7/Tools/Microsoft.VisualStudio.DevShell.dll'
Enter-VsDevShell -VsInstallPath 'D:/software/Microsoft Visual Studio/2022/Community' -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
$binDir = 'C:/Users/zgl18/.codex/worktrees/486b/RobWork/build/RobWorkStudio/bin/Release'
$env:QT_QPA_PLATFORM = 'windows'
$env:QT_QPA_PLATFORM_PLUGIN_PATH = 'D:/software/Qt/6.11.1/msvc2022_64/plugins/platforms'
$env:PATH = "$binDir;C:/Users/zgl18/.codex/worktrees/486b/RobWork/build/RobWorkStudio/libs/Release;C:/Users/zgl18/.codex/worktrees/486b/RobWork/build/RobWork/libs/Release;C:/Users/zgl18/.codex/worktrees/486b/RobWork/build/RobWorkSim/libs/Release;D:/10_Source_Repos/21_robot/RobWork/vcpkg/installed/x64-windows/bin;D:/software/Qt/6.11.1/msvc2022_64/bin;D:/software/Miniconda3;" + $env:PATH
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$outPath = (Resolve-Path -LiteralPath $OutputDir).Path
$env:IRD_UI_PLUGIN_SMOKE = $Smoke
$env:IRD_UI_PLUGIN_SMOKE_OUT = $outPath
$process = Start-Process -FilePath "$binDir/sdurws_ird_studio.exe" -WorkingDirectory $binDir -WindowStyle Hidden -PassThru -RedirectStandardOutput "$outPath/stdout.log" -RedirectStandardError "$outPath/stderr.log"
if ($Wait) {
    # 自动回归若被意外模态框阻塞，应保留日志并明确失败，不无限等待或计为通过。
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        Stop-Process -Id $process.Id
        Write-Error "studio smoke=$Smoke timeout after $TimeoutSeconds seconds" -ErrorAction Continue
        exit 124
    }
    $process.Refresh()
    Write-Host "studio smoke=$Smoke exit=$($process.ExitCode)"
    exit $process.ExitCode
}
Write-Host "studio pid=$($process.Id)"
