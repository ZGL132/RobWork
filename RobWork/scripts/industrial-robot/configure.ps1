# 工业机械臂重构·统一配置入口（WP-01-T03）
# 职责：按 WP-01 计划 §4.2 固定命令执行 CMake 配置——从仓库根解析绝对 -S/-B（不依赖当前
#       工作目录），经 VS x64 环境发现（common.ps1 §5.2 实现）后调用 cmake，配置输出、
#       关键选项与环境版本记录写入 <LogDirectory>\configure.log，退出码透传。
# 允许依赖：common.ps1、cmake.exe、VS x64 工具链；禁止：网络、自动删除既有构建目录、
#       自动修复源码或用户环境变量（CMAKE_PREFIX_PATH 等仅记录不设置）。
# 公共所有者：WP-01；契约：work-packages/WP-01 §4.2/§5.1～§5.3；architecture/testing-contract.md §5。
# Task ID：WP-01-T03；运行环境：Windows PowerShell 5.1+（无 PS7 专属语法）、VS x64。
#
# ★ 2026-10-16 CI 激活适配（gov-ci-activation，所有者裁决"按建议激活"）：本文件以
#   eb777e2（codex/wp-01-t04-gitlab-gate-r2）参考实现为基线，做两处选项级适配以匹配
#   今日构建树，其余逐字保留：
#   1) 固定选项新增 -DRWS_BUILD_INDUSTRIALROBOT=ON——该 option 由框架补丁
#      0001-rwslibs-entry-option.patch（2026-09-09）引入，OFF 时 industrialrobot 子树
#      整体不进构建（8 月验证运行早于该补丁，故当时无需传递）；今日 CI 不传则
#      46 个测试目标一个都不存在，run-tests 必然"零匹配"失败。
#   2) 固定选项移除 -DIRD_BUILD_BUSINESS_PLUGINS=OFF——全树 CMake 已无任何消费者
#      （grep 证实零引用，WP-01 时代遗留死选项）；保留只会误导读者以为该开关仍有效。
#   适配依据与登记：ci/README.md「2026-10-16 激活」节＋patches/PATCHES.md 0003 号。
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [string]$BuildDirectory = '',
    [string]$SourceDirectory = '',
    [string]$Generator = 'Visual Studio 17 2022',
    [string]$Platform = 'x64',
    [switch]$NoConfigure,
    [string]$LogDirectory = ''
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'common.ps1')

$repoRoot = Get-IndustrialRobotRepoRoot -ScriptRoot $PSScriptRoot   # 内层 RobWork：CMake -S 源根
$repoOuter = Split-Path -Parent $repoRoot                            # 外层仓库根：out\ 构建与证据根

# ---- 公共参数解析：输入路径 Resolve-Path 校验在前，任何输出目录创建在此之前不得发生 ----
$sourceDir = Resolve-IndustrialRobotSourceDirectory -Path $SourceDirectory -Default $repoRoot -Label '-SourceDirectory'
if ($null -eq $sourceDir) { exit 1 }
$buildDir = New-IndustrialRobotOutputDirectory -Path $BuildDirectory -Default (Join-Path $repoOuter 'out\build\industrial-robot') -Label '-BuildDirectory'
if ($null -eq $buildDir) { exit 1 }
if ([string]::IsNullOrWhiteSpace($LogDirectory)) {
    $logDir = New-IndustrialRobotLogDir -RepoRoot $repoRoot -TimeStamp (Get-IndustrialRobotTimeStamp)
}
else {
    $logDir = New-IndustrialRobotOutputDirectory -Path $LogDirectory -Default '' -Label '-LogDirectory'
}
if ($null -eq $logDir -or $logDir -eq '') { exit 1 }
$logFile = Join-Path $logDir 'configure.log'

# ---- 日志头：命令行、参数、路径、固定选项与环境记录（WP-01 计划 §4.2/§5.2）----
$prefixRecord = '<未设置>'
if ($null -ne $env:CMAKE_PREFIX_PATH -and $env:CMAKE_PREFIX_PATH -ne '') { $prefixRecord = $env:CMAKE_PREFIX_PATH }
Write-IndustrialRobotLog -LogFile $logFile -Line ('==== configure.ps1（WP-01-T03） ' + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss') + ' ====')
Write-IndustrialRobotLog -LogFile $logFile -Line ('[参数] Configuration=' + $Configuration + ' Generator="' + $Generator + '" Platform=' + $Platform + ' NoConfigure=' + $NoConfigure.IsPresent)
Write-IndustrialRobotLog -LogFile $logFile -Line ('[路径] SourceDirectory=' + $sourceDir)
Write-IndustrialRobotLog -LogFile $logFile -Line ('[路径] BuildDirectory=' + $buildDir)
Write-IndustrialRobotLog -LogFile $logFile -Line ('[路径] LogDirectory=' + $logDir)
Write-IndustrialRobotLog -LogFile $logFile -Line ('[选项] -DWITH_RWS=ON -DWITH_RWSIM=ON -DBUILD_TESTING=ON -DRWS_BUILD_INDUSTRIALROBOT=ON（WP-01 计划 §4.2 固定选项＋2026-10-16 激活适配，见文件头说明）')
Write-IndustrialRobotLog -LogFile $logFile -Line ('[环境] CMAKE_PREFIX_PATH=' + $prefixRecord + '（继承自操作员环境，脚本不修改）')
if ($NoConfigure) {
    Write-IndustrialRobotLog -LogFile $logFile -Line '[参数] -NoConfigure：本入口仅执行配置，开关按公共参数契约接受并记录，行为不变。'
}

# ---- VS x64 环境发现与版本记录（WP-01 计划 §5.2；找不到即失败，不回退 x86/MinGW）----
Write-IndustrialRobotLog -LogFile $logFile -Line '---- VS x64 环境发现 ----'
$vs = Initialize-IndustrialRobotVsEnvironment -LogFile $logFile
if (-not $vs.Success) {
    Write-IndustrialRobotLog -LogFile $logFile -Line ('[失败] ' + $vs.Diagnostic)
    [Console]::Error.WriteLine($vs.Diagnostic)
    [Console]::Error.WriteLine('[结果] configure 失败，退出码 1；日志保留: ' + $logFile)
    exit 1
}
Write-IndustrialRobotLog -LogFile $logFile -Line ('[环境] 发现方式=' + $vs.Via)
Write-IndustrialRobotLog -LogFile $logFile -Line ('[环境] MSVC=' + $vs.MsvcLine)
Write-IndustrialRobotLog -LogFile $logFile -Line ('[环境] Windows SDK=' + $vs.SdkVersion)
Write-IndustrialRobotLog -LogFile $logFile -Line ('[环境] ' + $vs.CMakeLine)
Write-IndustrialRobotLog -LogFile $logFile -Line ('[环境] ' + $vs.CTestLine)

# ---- §4.2 固定配置命令（生成器/架构来自参数缺省，选项集冻结）----
$cmake = (Get-Command -Name 'cmake.exe' -ErrorAction SilentlyContinue).Source
$cmakeArgs = @(
    '-S', $sourceDir,
    '-B', $buildDir,
    '-G', $Generator,
    '-A', $Platform,
    '-DWITH_RWS=ON',
    '-DWITH_RWSIM=ON',
    '-DBUILD_TESTING=ON',
    # 产品子树构建开关（框架补丁 0001 引入的 option，默认 OFF）——不传则 industrialrobot
    # 整体不进构建图，CI 测试面为空（2026-10-16 激活适配，缘由见文件头说明）
    '-DRWS_BUILD_INDUSTRIALROBOT=ON'
)
Write-IndustrialRobotLog -LogFile $logFile -Line '---- CMake 配置输出 ----'
$run = Invoke-IndustrialRobotNative -FilePath $cmake -ArgumentList $cmakeArgs -LogFile $logFile -Label 'cmake 配置'
Write-IndustrialRobotLog -LogFile $logFile -Line ('[结果] 退出码=' + $run.ExitCode + '；日志: ' + $logFile)
exit $run.ExitCode
