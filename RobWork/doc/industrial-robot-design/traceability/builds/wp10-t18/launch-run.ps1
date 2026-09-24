# UI-T18 宿主冒烟启动器（-r4 返工轮）——F-320 PATH 五项前置＋--rwsplugin 直载通道。
# 用法：pwsh -File launch-run.ps1 <运行目录 run1|run2>
param([Parameter(Mandatory=$true)][string]$RunDir)

$wt     = "D:/10_Source_Repos/21_robot/RobWork/.wt-wp10-t18"
$binDir = "$wt/build/RobWorkStudio/bin/Release"
$rwBin  = "$wt/build/RobWork/bin/Release"
$vcpkg  = "D:/10_Source_Repos/21_robot/RobWork/vcpkg/installed/x64-windows/bin"
$qtBin  = "D:/software/Qt/6.11.1/msvc2022_64/bin"
$conda  = "D:/software/Miniconda3"
$dll    = "$wt/build/RobWorkStudio/libs/Release/sdurws_ird_ui_plugin.dll"

# PATH 前置顺序＝UI-T17 移交手册 §1 实测清单（exe/Qt/boost 一批→RobWork 模块
# DLL→vcpkg boost_filesystem→Qt6*→python313.dll）。
$env:PATH = "$binDir;$rwBin;$vcpkg;$qtBin;$conda;" + $env:PATH

$cwd = "$wt/host-smoke-r4/$RunDir"
New-Item -ItemType Directory -Force -Path $cwd | Out-Null
Set-Location $cwd

# 直载通道：--rwsplugin <插件 DLL 绝对路径>（框架 RobWorkStudioApp 支持，
# "not to be confused with '--rwplugin'"）。控制台输出捕获至启动日志
# （[ird-ui-plugin] 装载门控行出线在此）。
$p = Start-Process -FilePath "$binDir/RobWorkStudio.exe" `
        -ArgumentList "--rwsplugin", "`"$dll`"" `
        -WorkingDirectory $cwd -PassThru -RedirectStandardOutput "$cwd/console-stdout.log" `
        -RedirectStandardError "$cwd/console-stderr.log"
Write-Output "LAUNCHED_PID=$($p.Id)"
