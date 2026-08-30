# 工业机械臂重构·统一测试入口（WP-01-T03）
# 职责：CTest 调用（默认 -j1 单进程）、-Regex 精确筛选（先以 ctest -N 预检，零匹配即
#       非零并报告）、GUI 规则（testing-contract §5、WP-01 计划 §6）：检测到继承
#       QT_*/QML_* 冲突变量先报告并停止；通过后为测试子进程强制 QT_QPA_PLATFORM=windows。
#       输出与退出码写入 <LogDirectory>\test.log 并透传；失败保留日志，不删除构建目录。
# 允许依赖：common.ps1、ctest.exe、VS x64 工具链；禁止：虚拟平台设置、多 GUI 进程并行、
#       自动修复源码或用户环境变量（强制 windows 平台为 testing-contract §5 冻结规则）。
# 公共所有者：WP-01；契约：work-packages/WP-01 §6；architecture/testing-contract.md §5。
# Task ID：WP-01-T03；运行环境：Windows PowerShell 5.1+（无 PS7 专属语法）、VS x64。
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [string]$BuildDirectory = '',
    [string]$SourceDirectory = '',
    [string]$Generator = 'Visual Studio 17 2022',
    [string]$Platform = 'x64',
    [switch]$NoConfigure,
    [string]$LogDirectory = '',
    [Parameter(Mandatory = $true)]
    [string]$Regex
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'common.ps1')

$repoRoot = Get-IndustrialRobotRepoRoot -ScriptRoot $PSScriptRoot
$repoOuter = Split-Path -Parent $repoRoot

# ---- 日志目录先行：任何失败路径都必须保留 test.log ----
if ([string]::IsNullOrWhiteSpace($LogDirectory)) {
    $logDir = New-IndustrialRobotLogDir -RepoRoot $repoRoot -TimeStamp (Get-IndustrialRobotTimeStamp)
}
else {
    $logDir = New-IndustrialRobotOutputDirectory -Path $LogDirectory -Default '' -Label '-LogDirectory'
}
if ($null -eq $logDir -or $logDir -eq '') { exit 1 }
$logFile = Join-Path $logDir 'test.log'

Write-IndustrialRobotLog -LogFile $logFile -Line ('==== run-tests.ps1（WP-01-T03） ' + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss') + ' ====')
Write-IndustrialRobotLog -LogFile $logFile -Line ('[参数] Configuration=' + $Configuration + ' Regex=' + $Regex + ' NoConfigure=' + $NoConfigure.IsPresent)
if ($NoConfigure) {
    Write-IndustrialRobotLog -LogFile $logFile -Line '[参数] -NoConfigure：本入口只消费上一步构建工件，开关按公共参数契约接受，行为不变。'
}

# ---- GUI 规则：冲突变量先报告并停止（优先于一切输入校验与 ctest 调用）----
$conflicts = @(Get-IndustrialRobotQtEnvironmentConflicts)
if ($conflicts.Count -gt 0) {
    Write-IndustrialRobotLog -LogFile $logFile -Line '[失败] 检测到继承的 QT_*/QML_* 冲突环境变量（testing-contract §5），先报告并停止：'
    foreach ($conflict in $conflicts) {
        Write-IndustrialRobotLog -LogFile $logFile -Line ('  冲突变量: ' + $conflict)
        [Console]::Error.WriteLine('[GUI 规则] 冲突变量: ' + $conflict)
    }
    Write-IndustrialRobotLog -LogFile $logFile -Line '[修复动作] 清理上述继承变量后重跑；Windows 测试平台固定为 windows，禁止虚拟平台与多 GUI 进程。'
    [Console]::Error.WriteLine('[GUI 规则] 检测到继承 QT_*/QML_* 冲突变量，已先报告并停止；清理后重跑。')
    [Console]::Error.WriteLine('[结果] run-tests 失败，退出码 1；日志保留: ' + $logFile)
    exit 1
}
if ($env:QT_QPA_PLATFORM -ine 'windows') {
    $env:QT_QPA_PLATFORM = 'windows'
    Write-IndustrialRobotLog -LogFile $logFile -Line '[GUI 规则] 已为测试子进程强制 QT_QPA_PLATFORM=windows（testing-contract §5）。'
}
Write-IndustrialRobotLog -LogFile $logFile -Line '[GUI 规则] 单进程串行：ctest -j 1；一次只运行一个测试可执行文件。'

# ---- 输入工件校验：构建目录必须已配置（每一步只消费上一步成功工件）----
$sourceDir = Resolve-IndustrialRobotSourceDirectory -Path $SourceDirectory -Default $repoRoot -Label '-SourceDirectory'
if ($null -eq $sourceDir) { exit 1 }
$buildDir = Resolve-IndustrialRobotSourceDirectory -Path $BuildDirectory -Default (Join-Path $repoOuter 'out\build\industrial-robot') -Label '-BuildDirectory'
if ($null -eq $buildDir) { exit 1 }
if (-not (Test-Path -LiteralPath (Join-Path $buildDir 'CMakeCache.txt'))) {
    [Console]::Error.WriteLine(('[入口参数] -BuildDirectory 不是已配置的构建目录（缺 CMakeCache.txt）: ' + $buildDir))
    [Console]::Error.WriteLine('[结果] run-tests 失败，退出码 1；日志保留: ' + $logFile)
    Write-IndustrialRobotLog -LogFile $logFile -Line ('[失败] -BuildDirectory 不是已配置的构建目录（缺 CMakeCache.txt）: ' + $buildDir)
    exit 1
}
Write-IndustrialRobotLog -LogFile $logFile -Line ('[路径] BuildDirectory=' + $buildDir)
Write-IndustrialRobotLog -LogFile $logFile -Line ('[路径] LogDirectory=' + $logDir)

# ---- VS x64 环境发现与版本记录（WP-01 计划 §5.2，与 configure/build 同一口径）----
Write-IndustrialRobotLog -LogFile $logFile -Line '---- VS x64 环境发现 ----'
$vs = Initialize-IndustrialRobotVsEnvironment -LogFile $logFile
if (-not $vs.Success) {
    Write-IndustrialRobotLog -LogFile $logFile -Line ('[失败] ' + $vs.Diagnostic)
    [Console]::Error.WriteLine($vs.Diagnostic)
    [Console]::Error.WriteLine('[结果] run-tests 失败，退出码 1；日志保留: ' + $logFile)
    exit 1
}
Write-IndustrialRobotLog -LogFile $logFile -Line ('[环境] 发现方式=' + $vs.Via)
Write-IndustrialRobotLog -LogFile $logFile -Line ('[环境] MSVC=' + $vs.MsvcLine + '；Windows SDK=' + $vs.SdkVersion)
Write-IndustrialRobotLog -LogFile $logFile -Line ('[环境] ' + $vs.CMakeLine + '；' + $vs.CTestLine)

$ctest = (Get-Command -Name 'ctest.exe' -ErrorAction SilentlyContinue).Source

# ---- Regex 精确筛选：先 ctest -N 预检数量，零匹配即非零并报告（不猜测、不降级）----
Write-IndustrialRobotLog -LogFile $logFile -Line '---- 测试清单预检（ctest -N）----'
$listing = Invoke-IndustrialRobotNative -FilePath $ctest `
    -ArgumentList @('--test-dir', $buildDir, '-C', $Configuration, '-N', '-R', $Regex) `
    -LogFile $logFile -Label ('ctest -N -R ' + $Regex)
$total = $null
foreach ($line in $listing.Lines) {
    if ($line -match 'Total Tests:\s*(\d+)') { $total = [int]$Matches[1] }
}
if ($null -eq $total) {
    $diagnostic = '[失败] 无法从 ctest -N 输出解析测试数量（预期 Total Tests 行）；不猜测，停止。'
    Write-IndustrialRobotLog -LogFile $logFile -Line $diagnostic
    [Console]::Error.WriteLine($diagnostic)
    [Console]::Error.WriteLine('[结果] run-tests 失败，退出码 1；日志保留: ' + $logFile)
    exit 1
}
if ($total -eq 0) {
    $diagnostic = '[失败] Regex 零匹配：-R "' + $Regex + '" 未匹配任何已注册测试（共 0 项）。'
    Write-IndustrialRobotLog -LogFile $logFile -Line $diagnostic
    [Console]::Error.WriteLine($diagnostic)
    [Console]::Error.WriteLine('[结果] run-tests 失败，退出码 1；日志保留: ' + $logFile)
    exit 1
}
Write-IndustrialRobotLog -LogFile $logFile -Line ('[预检] Regex 匹配测试数=' + $total)

# ---- CTest 执行：默认 -j1，退出码透传 ----
Write-IndustrialRobotLog -LogFile $logFile -Line '---- CTest 执行 ----'
$run = Invoke-IndustrialRobotNative -FilePath $ctest `
    -ArgumentList @('--test-dir', $buildDir, '-C', $Configuration, '-R', $Regex, '-j', '1') `
    -LogFile $logFile -Label ('ctest -R ' + $Regex + ' -j 1')
Write-IndustrialRobotLog -LogFile $logFile -Line ('[结果] 退出码=' + $run.ExitCode + '；日志: ' + $logFile)
exit $run.ExitCode
