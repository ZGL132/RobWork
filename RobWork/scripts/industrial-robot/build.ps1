# 工业机械臂重构·统一构建入口（WP-01-T03）
# 职责：按 WP-01 计划 §5.3 数据流执行"配置（-NoConfigure 可跳过）→ 构建目标"。
#       目标集合为本任务卡模型测试链路（configure→build→run-tests '^sdurws_ird_core_test$'）
#       所需的 sdurws_ird_core 与 sdurws_ird_core_test——WP-01-T02 交付并经独立验证可构建；
#       GUI 等其余模块目标链路属后续任务卡，不在本入口构建。构建输出与退出码写入
#       <LogDirectory>\build.log 并透传；失败保留日志，不自动删除既有构建目录。
# 允许依赖：common.ps1、cmake.exe、VS x64 工具链；禁止：网络、自动修复源码或环境变量。
# 公共所有者：WP-01；契约：work-packages/WP-01 §5；architecture/testing-contract.md §5。
# Task ID：WP-01-T03；运行环境：Windows PowerShell 5.1+（无 PS7 专属语法）、VS x64。
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

$repoRoot = Get-IndustrialRobotRepoRoot -ScriptRoot $PSScriptRoot
$repoOuter = Split-Path -Parent $repoRoot

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
$logFile = Join-Path $logDir 'build.log'

$prefixRecord = '<未设置>'
if ($null -ne $env:CMAKE_PREFIX_PATH -and $env:CMAKE_PREFIX_PATH -ne '') { $prefixRecord = $env:CMAKE_PREFIX_PATH }
Write-IndustrialRobotLog -LogFile $logFile -Line ('==== build.ps1（WP-01-T03） ' + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss') + ' ====')
Write-IndustrialRobotLog -LogFile $logFile -Line ('[参数] Configuration=' + $Configuration + ' NoConfigure=' + $NoConfigure.IsPresent)
Write-IndustrialRobotLog -LogFile $logFile -Line ('[路径] SourceDirectory=' + $sourceDir)
Write-IndustrialRobotLog -LogFile $logFile -Line ('[路径] BuildDirectory=' + $buildDir)
Write-IndustrialRobotLog -LogFile $logFile -Line ('[路径] LogDirectory=' + $logDir)
Write-IndustrialRobotLog -LogFile $logFile -Line ('[目标] sdurws_ird_core、sdurws_ird_core_test（WP-01-T03 模型测试链路目标集）')
Write-IndustrialRobotLog -LogFile $logFile -Line ('[环境] CMAKE_PREFIX_PATH=' + $prefixRecord + '（继承自操作员环境，脚本不修改）')

Write-IndustrialRobotLog -LogFile $logFile -Line '---- VS x64 环境发现 ----'
$vs = Initialize-IndustrialRobotVsEnvironment -LogFile $logFile
if (-not $vs.Success) {
    Write-IndustrialRobotLog -LogFile $logFile -Line ('[失败] ' + $vs.Diagnostic)
    [Console]::Error.WriteLine($vs.Diagnostic)
    [Console]::Error.WriteLine('[结果] build 失败，退出码 1；日志保留: ' + $logFile)
    exit 1
}
Write-IndustrialRobotLog -LogFile $logFile -Line ('[环境] 发现方式=' + $vs.Via)
Write-IndustrialRobotLog -LogFile $logFile -Line ('[环境] MSVC=' + $vs.MsvcLine + '；Windows SDK=' + $vs.SdkVersion)
Write-IndustrialRobotLog -LogFile $logFile -Line ('[环境] ' + $vs.CMakeLine + '；' + $vs.CTestLine)

$cmake = (Get-Command -Name 'cmake.exe' -ErrorAction SilentlyContinue).Source

# ---- 配置步骤：默认执行以保证入口可独立运行；-NoConfigure 跳过（WP-01 计划 §5.1）----
if (-not $NoConfigure) {
    $cmakeArgs = @(
        '-S', $sourceDir,
        '-B', $buildDir,
        '-G', $Generator,
        '-A', $Platform,
        '-DWITH_RWS=ON',
        '-DWITH_RWSIM=ON',
        '-DBUILD_TESTING=ON',
        '-DIRD_BUILD_BUSINESS_PLUGINS=OFF'
    )
    Write-IndustrialRobotLog -LogFile $logFile -Line '---- 配置步骤（WP-01 计划 §4.2 固定命令）----'
    $cfg = Invoke-IndustrialRobotNative -FilePath $cmake -ArgumentList $cmakeArgs -LogFile $logFile -Label 'cmake 配置'
    if ($cfg.ExitCode -ne 0) {
        Write-IndustrialRobotLog -LogFile $logFile -Line ('[结果] 配置失败，透传退出码=' + $cfg.ExitCode + '；日志保留: ' + $logFile)
        [Console]::Error.WriteLine('[结果] build 失败于配置步骤，退出码 ' + $cfg.ExitCode + '；日志保留: ' + $logFile)
        exit $cfg.ExitCode
    }
}
else {
    Write-IndustrialRobotLog -LogFile $logFile -Line '[参数] -NoConfigure：跳过配置步骤，直接构建（消费既有构建目录工件）。'
}

# ---- 构建步骤：逐目标透传失败 ----
foreach ($target in @('sdurws_ird_core', 'sdurws_ird_core_test')) {
    Write-IndustrialRobotLog -LogFile $logFile -Line ('---- 构建目标 ' + $target + ' ----')
    $run = Invoke-IndustrialRobotNative -FilePath $cmake `
        -ArgumentList @('--build', $buildDir, '--config', $Configuration, '--target', $target) `
        -LogFile $logFile -Label ('cmake --build --target ' + $target)
    if ($run.ExitCode -ne 0) {
        Write-IndustrialRobotLog -LogFile $logFile -Line ('[结果] 构建失败，透传退出码=' + $run.ExitCode + '；日志保留: ' + $logFile)
        [Console]::Error.WriteLine('[结果] build 失败于目标 ' + $target + '，退出码 ' + $run.ExitCode + '；日志保留: ' + $logFile)
        exit $run.ExitCode
    }
}

Write-IndustrialRobotLog -LogFile $logFile -Line ('[结果] 退出码=0；日志: ' + $logFile)
exit 0
