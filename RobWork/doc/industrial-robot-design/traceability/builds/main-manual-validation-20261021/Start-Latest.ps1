# 本机手工验收入口：绑定本次主线构建和程序摘要，防止误启D盘旧工作树程序。
# 此文件是可复跑的交付辅助脚本，不改变产品行为；只支持本机已配置的VS/Qt环境。
param(
    [string]$RepoRoot='C:/Users/zgl18/.codex/worktrees/3582/RobWork',
    [switch]$CheckOnly
)
$ErrorActionPreference='Stop'
$outputDir=Join-Path $RepoRoot 'build/manual-validation-20261021'
$manifest=Get-Content -Raw -LiteralPath (Join-Path $outputDir 'manual-build.json') | ConvertFrom-Json
$binary=Join-Path $RepoRoot 'build/RobWorkStudio/bin/Release/sdurws_ird_studio.exe'

# 主线允许后续仅追加文档记录；任何源码差异或二进制替换都需要重新构建和留痕。
if((& git -C $RepoRoot branch --show-current) -ne 'redesign-main'){throw '请使用redesign-main工作树'}
& git -C $RepoRoot diff --quiet $manifest.sourceCommit HEAD -- RobWork/RobWorkStudio RobWork/RobWork RobWork/RobWorkSim RobWork/CMakeLists.txt
if($LASTEXITCODE -ne 0){throw '主线源码已变化，请重新构建后更新验收入口'}
if(& git -C $RepoRoot status --porcelain=v1 --untracked-files=no -- RobWork/RobWorkStudio RobWork/RobWork RobWork/RobWorkSim RobWork/CMakeLists.txt){throw '源码存在未提交修改，请先确认验收版本'}
if((Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash -ne $manifest.sha256){throw '程序摘要不符，不能作为本次验收版本'}
Write-Host ('开发主线：'+(& git -C $RepoRoot rev-parse --short HEAD))
Write-Host ('构建源码：'+$manifest.sourceCommit)
Write-Host ('程序路径：'+$binary)
if($CheckOnly){exit 0}

# 与真实窗口回归使用同一VS x64和windows平台环境；正常显示窗口供人工操作。
Import-Module 'D:/software/Microsoft Visual Studio/2022/Community/Common7/Tools/Microsoft.VisualStudio.DevShell.dll'
Enter-VsDevShell -VsInstallPath 'D:/software/Microsoft Visual Studio/2022/Community' -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
$env:QT_QPA_PLATFORM='windows'
$env:QT_QPA_PLATFORM_PLUGIN_PATH='D:/software/Qt/6.11.1/msvc2022_64/plugins/platforms'
$env:PATH="$RepoRoot/build/RobWorkStudio/bin/Release;$RepoRoot/build/RobWorkStudio/libs/Release;$RepoRoot/build/RobWork/libs/Release;$RepoRoot/build/RobWorkSim/libs/Release;D:/10_Source_Repos/21_robot/RobWork/vcpkg/installed/x64-windows/bin;D:/software/Qt/6.11.1/msvc2022_64/bin;D:/software/Miniconda3;"+$env:PATH
# 自动tour变量只作用于验证过程，人工入口必须进入正常工作台。
$env:IRD_UI_PLUGIN_SMOKE=$null
$env:IRD_UI_PLUGIN_SMOKE_OUT=$null
Start-Process -FilePath $binary -WorkingDirectory $RepoRoot -WindowStyle Normal | Out-Null
