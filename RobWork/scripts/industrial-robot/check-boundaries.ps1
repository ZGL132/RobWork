# 工业机械臂重构·构建边界门禁入口（WP-01-T01 ird_gates 执行器）
# 职责：在已配置的 CMake 构建目录上构建并执行 ird_gates 门禁目标——依赖红线
#       （R-1/R-2/R-3）、补丁登记一致性（SA-02）等边界规则的唯一权威实现
#       （cmake/ird_gates.cmake＋ird_gates_whitelist.cmake，随 WP-01-T01 交付并
#       随任务卡持续演进），违规即非零退出并输出"规则＋目标＋修复动作"诊断；
#       不做任何自动修复。
# 允许依赖：common.ps1、cmake.exe、VS x64 工具链、构建目录中已配置的 ird_gates
#           目标；禁止：网络、源码修改、构建目录删除。
# 契约引用：work-packages/WP-01 §7（边界规则）；architecture/testing-contract.md §5；
#           gate-all.ps1 第 2 步（本地门禁对 ird_gates 的同款调用口径）。
# Task ID：WP-01-T01（ird_gates）＋ gov-ci-activation（本执行器，2026-10-16）；
#          运行环境：Windows PowerShell 5.1+（无 PS7 专属语法）、VS x64。
#
# ★ 2026-10-16 CI 激活适配（gov-ci-activation，所有者裁决"按建议激活"）：本入口由
#   eb777e2 参考实现的【静态正则扫描器】整体替换为【ird_gates 构建期门禁运行器】。
#   替换动因（两条均为今日树的实证缺陷，非风格偏好）：
#   1) 静态扫描器的 R3 白名单正则停留在 WP-01 时代目标命名（sdurws_ird_<unit>(_test)?），
#      对今日合法命名（sdurws_ird_<unit>_contract_test／_gui_test／_perf_test，下划线
#      多段）必然误报"未登记 target"——静态扫描器在今日树上把 46 个测试目标中的
#      非 _test 后缀者全部误杀，boundaries job 恒红；
#   2) ird_gates 是项目实际维护的边界权威（gate-all.ps1 第 2 步、AGENTS §6.3 每次提交
#      自查均调用它；其白名单已覆盖 GTest::、contract_test 命名等今日形态），CI 应与
#      本地门禁同源而非并行维护第二套规则。
#   前置：-BuildDirectory 必须已是 configure.ps1 配置过的构建目录（本 job 消费上游
#   工件）；ird_gates 目标由 industrialrobot/CMakeLists 注册。
#   登记依据：ci/README.md「2026-10-16 激活」节＋patches/PATCHES.md 0003 号。
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [string]$BuildDirectory = '',
    [string]$SourceDirectory = '',
    [string]$Generator = 'Visual Studio 17 2022',
    [string]$Platform = 'x64',
    [string]$LogDirectory = ''
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'common.ps1')

$repoRoot = Get-IndustrialRobotRepoRoot -ScriptRoot $PSScriptRoot

# ---- 日志目录先行：任何失败路径都必须保留 boundaries.log ----
if ([string]::IsNullOrWhiteSpace($LogDirectory)) {
    $logDir = New-IndustrialRobotLogDir -RepoRoot $repoRoot -TimeStamp (Get-IndustrialRobotTimeStamp)
}
else {
    $logDir = New-IndustrialRobotOutputDirectory -Path $LogDirectory -Default '' -Label '-LogDirectory'
}
if ($null -eq $logDir -or $logDir -eq '') { exit 1 }
$logFile = Join-Path $logDir 'boundaries.log'

Write-IndustrialRobotLog -LogFile $logFile -Line ('==== check-boundaries.ps1（ird_gates 执行器，2026-10-16 激活适配） ' + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss') + ' ====')
Write-IndustrialRobotLog -LogFile $logFile -Line ('[参数] Configuration=' + $Configuration)

# ---- 输入工件校验：构建目录必须已配置（本入口消费上游 build 工件）----
$buildDir = Resolve-IndustrialRobotSourceDirectory -Path $BuildDirectory -Default (Join-Path (Split-Path -Parent $repoRoot) 'out\build\industrial-robot') -Label '-BuildDirectory'
if ($null -eq $buildDir) { exit 1 }
if (-not (Test-Path -LiteralPath (Join-Path $buildDir 'CMakeCache.txt'))) {
    [Console]::Error.WriteLine(('[入口参数] -BuildDirectory 不是已配置的构建目录（缺 CMakeCache.txt）: ' + $buildDir))
    [Console]::Error.WriteLine('[结果] check-boundaries 失败，退出码 1；日志保留: ' + $logFile)
    Write-IndustrialRobotLog -LogFile $logFile -Line ('[失败] -BuildDirectory 不是已配置的构建目录（缺 CMakeCache.txt）: ' + $buildDir)
    exit 1
}
Write-IndustrialRobotLog -LogFile $logFile -Line ('[路径] BuildDirectory=' + $buildDir)
Write-IndustrialRobotLog -LogFile $logFile -Line ('[路径] LogDirectory=' + $logDir)

# ---- VS x64 环境发现与版本记录（与 configure/build/run-tests 同一口径）----
Write-IndustrialRobotLog -LogFile $logFile -Line '---- VS x64 环境发现 ----'
$vs = Initialize-IndustrialRobotVsEnvironment -LogFile $logFile
if (-not $vs.Success) {
    Write-IndustrialRobotLog -LogFile $logFile -Line ('[失败] ' + $vs.Diagnostic)
    [Console]::Error.WriteLine($vs.Diagnostic)
    [Console]::Error.WriteLine('[结果] check-boundaries 失败，退出码 1；日志保留: ' + $logFile)
    exit 1
}
Write-IndustrialRobotLog -LogFile $logFile -Line ('[环境] 发现方式=' + $vs.Via)
Write-IndustrialRobotLog -LogFile $logFile -Line ('[环境] MSVC=' + $vs.MsvcLine + '；Windows SDK=' + $vs.SdkVersion)

$cmake = (Get-Command -Name 'cmake.exe' -ErrorAction SilentlyContinue).Source

# ---- ird_gates 门禁目标构建与执行 ----
# gate-all.ps1 第 2 步同款调用：ird_gates 为脚本驱动的自定义目标（cmake -P 门禁
# 脚本＋白名单），构建该目标即执行全部门禁检查；违规时目标构建失败（非零退出），
# 诊断行经 Invoke-IndustrialRobotNative 全量落盘并回显。
Write-IndustrialRobotLog -LogFile $logFile -Line '---- ird_gates（依赖红线/补丁门禁）----'
$run = Invoke-IndustrialRobotNative -FilePath $cmake `
    -ArgumentList @('--build', $buildDir, '--config', $Configuration, '--target', 'ird_gates') `
    -LogFile $logFile -Label 'cmake --build --target ird_gates'
if ($run.ExitCode -ne 0) {
    Write-IndustrialRobotLog -LogFile $logFile -Line ('[结果] ird_gates 门禁未过，透传退出码=' + $run.ExitCode + '；日志保留: ' + $logFile)
    [Console]::Error.WriteLine('[结果] check-boundaries 失败（ird_gates 退出码 ' + $run.ExitCode + '）；日志保留: ' + $logFile)
    exit $run.ExitCode
}
Write-IndustrialRobotLog -LogFile $logFile -Line '[结果] ird_gates 全绿；日志: ' + $logFile
Write-Output 'Boundary gate (ird_gates) passed.'
exit 0
