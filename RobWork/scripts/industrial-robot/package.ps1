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
#
# ★ 2026-10-16 CI 激活适配（gov-ci-activation，所有者裁决"按建议激活"）：收集面由
#   eb777e2 参考实现的单库口径（sdurws_ird_core.lib＋core include 白名单）扩为
#   【全单元口径】——收集全部 sdurws_ird_* 链接产物（.lib 静态库/导入库＋.dll 动态
#   库，限 $Configuration 配置目录）与全部单元 include/ 公共头。适配动因＝build 面
#   已扩至全量测试面（build.ps1 2026-10-16 适配），20 单元产品库均为已构建目标集
#   成员，单库出包与测试面失配。manifest 黑名单（testdata/_test/_p.hpp/pdb/绝对路径）
#   与"不执行全树 cmake --install"红线逐字保留。登记：ci/README.md「2026-10-16 激活」
#   节＋patches/PATCHES.md 0003 号。

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
Write-IndustrialRobotLog -LogFile $logFile -Line '[收集边界] 收集全单元 sdurws_ird_* 链接产物＋各单元 include/ 公共头，不执行全树 cmake --install（2026-08-31 所有者裁决红线保留；收集面 2026-10-16 激活适配，见文件头说明）。'

# ---- 收集 1/2：全单元 sdurws_ird_* 链接产物（父目录名必须等于目标配置，保证确定性）----
# .lib 覆盖静态库与 DLL 导入库，.dll 覆盖动态库/插件产物；_test 可执行文件不在收集
# 模式内（exe 后缀本就不匹配），manifest 黑名单另作第二道复核。
# 收集用 @() 累加而非 @(a,b) 管道展开——后者在单边为空、另一边仅 1 项时会把结果
# 解包成标量，StrictMode 2.0 下标量无 .Count 属性（PropertyNotFoundStrict）。
$artifactCandidates = @()
$artifactCandidates += @(Get-ChildItem -LiteralPath $buildPath -Recurse -Filter 'sdurws_ird_*.lib' -File |
    Where-Object { $_.Directory.Name -eq $Configuration })
$artifactCandidates += @(Get-ChildItem -LiteralPath $buildPath -Recurse -Filter 'sdurws_ird_*.dll' -File |
    Where-Object { $_.Directory.Name -eq $Configuration })
if ($artifactCandidates.Count -eq 0) {
    [Console]::Error.WriteLine('[打包前置] 构建目录中未找到 ' + $Configuration + ' 配置的任何 sdurws_ird_* 产物；请先执行 build.ps1 -Configuration ' + $Configuration + '。')
    Write-IndustrialRobotLog -LogFile $logFile -Line ('[失败] 未找到 ' + $Configuration + ' 配置的 sdurws_ird_* 产物，拒绝出包。')
    exit 1
}
$libDir = Join-Path $stagingPath 'lib'
New-Item -ItemType Directory -Path $libDir -Force | Out-Null
foreach ($candidate in $artifactCandidates) {
    Copy-Item -LiteralPath $candidate.FullName -Destination (Join-Path $libDir $candidate.Name) -Force
    Write-IndustrialRobotLog -LogFile $logFile -Line ('[收集] 产物: ' + $candidate.FullName + ' -> lib/' + $candidate.Name)
}

# ---- 收集 2/2：全单元 include/ 公共头（目录缺失视为空集，不因空头集失败）----
# 单元公共头布局＝<industrialrobot>\<unit>\include\sdurws\ird\<unit>\...；glob 限定
# 一级单元目录下的 include，避免误收叶子目录外的杂散头。
# 注意 $repoRoot 已是内层 RobWork（common.ps1 Get-IndustrialRobotRepoRoot 契约），
# 再拼一层 'RobWork\' 会得到不存在的双 RobWork 路径——eb777e2 参考实现的潜在笔误
# 当年因 core/include 缺席被 Test-Path 空集分支掩盖，本适配改为真实枚举后必须修正。
$irdSrcRoot = Join-Path $repoRoot 'RobWorkStudio\src\rwslibs\industrialrobot'
$includeRoots = @(Get-ChildItem -LiteralPath $irdSrcRoot -Directory |
    ForEach-Object { Join-Path $_.FullName 'include' } |
    Where-Object { Test-Path -LiteralPath $_ -PathType Container })
$headerCount = 0
foreach ($includeRoot in $includeRoots) {
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
Write-IndustrialRobotLog -LogFile $logFile -Line ('[边界] 全单元公共头集数量=' + $headerCount + '（单元 include/ 目录缺失视为空集，不因空头集失败）。')

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
    taskId = 'gov-ci-activation'   # 出包溯源批次（2026-10-16 CI 激活适配；schema 与字段名不变）
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
