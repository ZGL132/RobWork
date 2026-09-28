# WP-24-T03 验收 attempt 3 GUI 冒烟启动器（复刻 acc/WP-24-T03/2 的 acc-launch-run.ps1 先例，
# 仅将源树/构建树指向本次复现现场：detached worktree @ 16b6b718 的集成构建产物）。
# F-320 PATH 五项前置＋--rwsplugin 直载通道。
param([Parameter(Mandatory=$true)][string]$RunDir)

$wt     = "D:/tmp_acc_wp24t03_r2/wtA"
$binDir = "$wt/build/RobWorkStudio/bin/Release"
$rwBin  = "$wt/build/RobWork/bin/Release"
$vcpkg  = "D:/10_Source_Repos/21_robot/RobWork/vcpkg/installed/x64-windows/bin"
$qtBin  = "D:/software/Qt/6.11.1/msvc2022_64/bin"
$conda  = "D:/software/Miniconda3"
$dll    = "$wt/build/RobWorkStudio/libs/Release/sdurws_ird_ui_plugin.dll"

# PATH 前置顺序＝UI-T17 移交手册 §1 实测清单。
$env:PATH = "$binDir;$rwBin;$vcpkg;$qtBin;$conda;" + $env:PATH

$cwd = "D:/tmp_acc_wp24t03_r2/acc-evidence/gui-smoke/$RunDir"
New-Item -ItemType Directory -Force -Path $cwd | Out-Null
Set-Location $cwd

# 直载通道：--rwsplugin <插件 DLL 绝对路径>。
$p = Start-Process -FilePath "$binDir/RobWorkStudio.exe" `
        -ArgumentList "--rwsplugin", "`"$dll`"" `
        -WorkingDirectory $cwd -PassThru -RedirectStandardOutput "$cwd/console-stdout.log" `
        -RedirectStandardError "$cwd/console-stderr.log"
Write-Output "LAUNCHED_PID=$($p.Id)"
