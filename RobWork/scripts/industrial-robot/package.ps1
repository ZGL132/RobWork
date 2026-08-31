# 工业机械臂重构·安装打包入口（WP-01-T04 返工，2026-08-31 所有者裁决口径）
# 职责：消费已配置的 CMake 构建目录，按已构建目标集收集出包——仅收集 sdurws_ird_core
#       链接库产物与 core 模块 include/ 白名单公共头（阶段 A 允许头集为空、目录缺失视为
#       空集，不因空头集失败），生成安装 manifest 与压缩包；不执行全树 cmake --install
#       （industrialrobot/ 无 install(TARGETS)，全树安装必然触碰未构建的 RobWork 工件）。
# 允许依赖：common.ps1、Get-ChildItem/Copy-Item/Compress-Archive 等内置组件；禁止网络、
#           源码修改、构建目录删除与全树安装。
# 契约引用：work-packages/WP-01 §5/§9；architecture/testing-contract.md §4；
#           agent-tasks/WP-01-T04-gitlab-gate.md"所有者裁决"第 2 条；Task ID：WP-01-T04。
# 错误路径：任一前置缺失或黑名单命中即非零退出，保留日志与半成品安装树，不做自动修复。
# 公共所有者：WP-01（修改须经 WP-01 任务卡授权）；参数签名 Configuration/BuildDirectory/
#             LogDirectory 与 WP-01 计划 §5.1 冻结一致。

[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [string]$BuildDirectory = '',
    [string]$LogDirectory = ''
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'common.ps1')

$repoRoot = Get-IndustrialRobotRepoRoot -ScriptRoot $PSScriptRoot
$repoOuter = Split-Path -Parent $repoRoot

if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    $BuildDirectory = Join-Path $repoOuter 'out\build\industrial-robot'
}
$buildPath = Resolve-IndustrialRobotSourceDirectory -Path $BuildDirectory -Default '' -Label '-BuildDirectory'
if ($null -eq $buildPath) { exit 1 }
$cachePath = Join-Path $buildPath 'CMakeCache.txt'
if (-not (Test-Path -LiteralPath $cachePath -PathType Leaf)) {
    [Console]::Error.WriteLine('[打包前置] 构建目录缺少 CMakeCache.txt，拒绝打包: ' + $buildPath)
    exit 1
}

if ([string]::IsNullOrWhiteSpace($LogDirectory)) {
    $logPath = New-IndustrialRobotLogDir -RepoRoot $repoRoot -TimeStamp (Get-IndustrialRobotTimeStamp)
}
else {
    $logPath = New-IndustrialRobotOutputDirectory -Path $LogDirectory -Default '' -Label '-LogDirectory'
}
if ($null -eq $logPath -or $logPath -eq '') { exit 1 }
$logFile = Join-Path $logPath 'package.log'
$stagingPath = Join-Path $logPath 'install'
$archivePath = Join-Path $logPath ('industrial-robot-' + $Configuration + '.zip')
$manifestPath = Join-Path $stagingPath 'install-manifest.json'

Write-IndustrialRobotLog -LogFile $logFile -Line ('==== package.ps1（WP-01-T04 已构建目标集收集） ' + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss') + ' ====')
Write-IndustrialRobotLog -LogFile $logFile -Line ('[参数] Configuration=' + $Configuration)
Write-IndustrialRobotLog -LogFile $logFile -Line ('[路径] BuildDirectory=' + $buildPath)
Write-IndustrialRobotLog -LogFile $logFile -Line ('[路径] LogDirectory=' + $logPath)
Write-IndustrialRobotLog -LogFile $logFile -Line '[收集边界] 仅收集已构建目标集产物（sdurws_ird_core 链接库＋core include 白名单公共头），不执行全树 cmake --install（2026-08-31 所有者裁决）。'

# ---- 收集 1/2：sdurws_ird_core 链接库产物（父目录名必须等于目标配置，保证确定性）----
$libCandidates = @(Get-ChildItem -LiteralPath $buildPath -Recurse -Filter 'sdurws_ird_core.lib' -File |
    Where-Object { $_.Directory.Name -eq $Configuration })
if ($libCandidates.Count -eq 0) {
    [Console]::Error.WriteLine('[打包前置] 构建目录中未找到 ' + $Configuration + ' 配置的 sdurws_ird_core.lib；请先执行 build.ps1 -Configuration ' + $Configuration + '。')
    Write-IndustrialRobotLog -LogFile $logFile -Line ('[失败] 未找到 ' + $Configuration + ' 配置的 sdurws_ird_core.lib，拒绝出包。')
    exit 1
}
if ($libCandidates.Count -gt 1) {
    foreach ($candidate in $libCandidates) {
        Write-IndustrialRobotLog -LogFile $logFile -Line ('[失败] 候选歧义: ' + $candidate.FullName)
    }
    [Console]::Error.WriteLine('[打包前置] sdurws_ird_core.lib 存在多个 ' + $Configuration + ' 候选，拒绝歧义出包。')
    exit 1
}
$libDir = Join-Path $stagingPath 'lib'
New-Item -ItemType Directory -Path $libDir -Force | Out-Null
Copy-Item -LiteralPath $libCandidates[0].FullName -Destination (Join-Path $libDir 'sdurws_ird_core.lib') -Force
Write-IndustrialRobotLog -LogFile $logFile -Line ('[收集] 链接库: ' + $libCandidates[0].FullName + ' -> lib/sdurws_ird_core.lib')

# ---- 收集 2/2：core 模块 include/ 白名单公共头（阶段 A 允许为空；目录缺失视为空集）----
$includeRoot = Join-Path $repoRoot 'RobWork\RobWorkStudio\src\rwslibs\industrialrobot\core\include'
$headerCount = 0
if (Test-Path -LiteralPath $includeRoot -PathType Container) {
    $headerFiles = @(Get-ChildItem -LiteralPath $includeRoot -Recurse -File |
        Where-Object { $_.Extension -in '.h', '.hpp' })
    foreach ($header in $headerFiles) {
        $relativeFromInclude = $header.FullName.Substring($includeRoot.Length).TrimStart('\', '/') -replace '\\', '/'
        $destPath = Join-Path $stagingPath ('include\' + ($relativeFromInclude -replace '/', '\'))
        $destParent = Split-Path -Parent $destPath
        if (-not (Test-Path -LiteralPath $destParent -PathType Container)) {
            New-Item -ItemType Directory -Path $destParent -Force | Out-Null
        }
        Copy-Item -LiteralPath $header.FullName -Destination $destPath -Force
        $headerCount++
    }
}
Write-IndustrialRobotLog -LogFile $logFile -Line ('[边界] core 公共头集数量=' + $headerCount + '（阶段 A 允许为空；目录缺失视为空集）。')

# ---- manifest：相对路径＋SHA-256，逐文件与生成后双重黑名单复检 ----
$forbiddenPath = '(?i)(^|/)(testdata|testing|tests?)(/|$)|(^|/)[^/]*_test(?:\.[^/]*)?$|(^|/)[^/]*_p\.(?:h|hpp)$|\.(?:pdb|ilk)$'
$files = @(Get-ChildItem -LiteralPath $stagingPath -Recurse -File | Where-Object { $_.FullName -ne $manifestPath })
$entries = New-Object System.Collections.Generic.List[object]
foreach ($file in $files) {
    $relative = $file.FullName.Substring($stagingPath.Length).TrimStart('\', '/') -replace '\\', '/'
    if ($relative -match $forbiddenPath) {
        $diagnostic = '[安装边界] 禁止内容出现在安装树：' + $relative + '；移除测试数据、测试产物或私有头后重试。'
        [Console]::Error.WriteLine($diagnostic)
        Write-IndustrialRobotLog -LogFile $logFile -Line ('[失败] ' + $diagnostic)
        exit 1
    }
    if ($relative -match '^[A-Za-z]:[\\/]' -or $relative.StartsWith('/')) {
        $diagnostic = '[安装边界] 安装清单包含绝对路径：' + $relative
        [Console]::Error.WriteLine($diagnostic)
        Write-IndustrialRobotLog -LogFile $logFile -Line ('[失败] ' + $diagnostic)
        exit 1
    }
    $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    $entries.Add([ordered]@{ path = $relative; bytes = $file.Length; sha256 = $hash })
}

$revision = ''
try {
    $revision = (& git -C $repoOuter rev-parse HEAD 2>$null | Select-Object -First 1).Trim()
}
catch { $revision = '' }
$manifest = [ordered]@{
    schemaVersion = 1
    taskId = 'WP-01-T04'
    configuration = $Configuration
    gitRevision = $revision
    generatedAt = (Get-Date).ToUniversalTime().ToString('o')
    files = @($entries | Sort-Object -Property path)
}
$json = $manifest | ConvertTo-Json -Depth 6
if ($json -match '(?i)[A-Za-z]:[\\/]' -or $json -match '(?i)(?:testdata|\btests?\b|_p\.hpp)') {
    [Console]::Error.WriteLine('[安装边界] manifest 生成后包含禁止路径或内容名，拒绝出包。')
    Write-IndustrialRobotLog -LogFile $logFile -Line '[失败] manifest 生成后包含禁止路径或内容名。'
    exit 1
}
$json | Set-Content -LiteralPath $manifestPath -Encoding UTF8

if (Test-Path -LiteralPath $archivePath) { Remove-Item -LiteralPath $archivePath -Force }
Compress-Archive -Path (Join-Path $stagingPath '*') -DestinationPath $archivePath -Force
Write-IndustrialRobotLog -LogFile $logFile -Line ('[结果] 文件数=' + $entries.Count + '（含公共头 ' + $headerCount + '）；manifest=' + $manifestPath)
Write-IndustrialRobotLog -LogFile $logFile -Line ('[结果] archive=' + $archivePath + '；退出码=0')
Write-Output ('Package completed: ' + $archivePath)
exit 0
