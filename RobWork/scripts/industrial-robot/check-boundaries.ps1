# 工业机械臂重构·构建边界静态扫描器（WP-01-T01）
# 职责：对 sdurws_ird_* 目标树执行 WP-01 计划 §7 边界规则的静态扫描，违反任一规则
#       即非零退出并输出"规则＋文件＋行＋修复动作"诊断；不做任何自动修复。
# 允许依赖：PowerShell 5.1/7 内置组件、common.ps1（路径解析）；禁止调用网络与构建系统。
# 契约引用：architecture/public-interfaces.md §6.3（稳定端口与运行时名称所有者）、
#           architecture/testing-contract.md（GUI 启动规则）、work-packages/WP-01 §2/§7。
# Task ID：WP-01-T01；公共所有者：WP-01（修改须经 WP-01 任务卡授权）。
# 规则集（对应 WP-01 计划 §7）：
#   R1 核心公共头包含 QWidget/QApplication/QtWidgets/旧插件头（ui 与 plugins 目录按
#      §2.2 允许 Widgets，不在 R1 范围）；
#   R2 核心链接旧插件目标（robotmodelbuilder/engineeringrequirements/kinematicanalysis/
#      structureoptimizer*）；
#   R3 链接未登记 target（拓扑白名单：sdurws_ird_*、Qt、rw_*、RobWork 等，见 §2.1/§2.2）；
#   R4 业务插件自行拼接运行时名称（名称唯一所有者为 runtime 的 RuntimeNameMap）；
#   R5 业务插件声明碰撞默认值/安全距离（唯一所有者为 policy 库）；
#   R6 安装规则含测试数据/私有头/绝对构建路径；
#   R7 脚本出现虚拟平台（QT_QPA_PLATFORM 非 windows 值）设置或 GUI 并行启动。
# 依赖版本/许可证/哈希/审批审计由 WP-01-T05 扩展（WP-01 计划 §8），本扫描器不实现。
param([string]$ScanRoot = '')

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'common.ps1')
$repoRoot = Get-IndustrialRobotRepoRoot -ScriptRoot $PSScriptRoot

$violations = New-Object System.Collections.Generic.List[string]

function Add-BoundaryViolation {
    param([string]$Rule, [string]$File, [int]$Line, [string]$Detail, [string]$Fix)
    $script:violations.Add(('[{0}] {1}:{2} {3}；修复动作：{4}' -f $Rule, $File, $Line, $Detail, $Fix))
}

function Get-CMakeCommandBlocks {
    # 提取 CMake 文件中指定命令的完整括号块（含跨行），返回 @{Start=起始行下标; Text=块文本}。
    param([string[]]$Lines, [string]$Command)
    $result = New-Object System.Collections.Generic.List[object]
    $inBlock = $false; $depth = 0; $startLine = 0
    $current = New-Object System.Collections.Generic.List[string]
    for ($i = 0; $i -lt $Lines.Count; $i++) {
        $line = $Lines[$i]
        if (-not $inBlock) {
            if ($line -match ('(?i)^\s*' + [regex]::Escape($Command) + '\s*\(')) {
                $inBlock = $true; $depth = 0; $startLine = $i
                $current.Clear()
                $current.Add($line)
                $depth += ([regex]::Matches($line, '\(')).Count - ([regex]::Matches($line, '\)')).Count
                if ($depth -le 0) {
                    $inBlock = $false
                    $result.Add(@{ Start = $startLine; Text = ($current -join "`n") })
                }
            }
        }
        else {
            $current.Add($line)
            $depth += ([regex]::Matches($line, '\(')).Count - ([regex]::Matches($line, '\)')).Count
            if ($depth -le 0) {
                $inBlock = $false
                $result.Add(@{ Start = $startLine; Text = ($current -join "`n") })
            }
        }
    }
    return $result
}

function Test-CoreHeaderPath {
    # R1 范围：核心模块公共头。ui 与 plugins 目录按 WP-01 §2.2 允许 Widgets，不扫描。
    param([string]$RelativePath)
    return ($RelativePath -notmatch '(^|\\)(plugins|ui)(\\|$)')
}

function Read-TextLines {
    param([string]$Path)
    try {
        return [System.IO.File]::ReadAllLines($Path)
    }
    catch {
        Add-BoundaryViolation -Rule 'R0 IO' -File $Path -Line 0 -Detail ('文件不可读：' + $_.Exception.Message) -Fix '检查文件权限或编码后重试扫描。'
        return $null
    }
}

# ---- 扫描根解析 ----
$root = $null
$explicitScanRoot = ($ScanRoot -ne '')
if ($explicitScanRoot) {
    if (-not (Test-Path -LiteralPath $ScanRoot)) {
        [Console]::Error.WriteLine(('[R0 IO] 扫描根不存在或不可读: ' + $ScanRoot + '；请检查 -ScanRoot 参数。'))
        exit 1
    }
    $root = (Resolve-Path -LiteralPath $ScanRoot).Path
}
else {
    $defaultRoot = Join-Path $repoRoot 'RobWorkStudio\src\rwslibs\industrialrobot'
    if (Test-Path -LiteralPath $defaultRoot) {
        $root = $defaultRoot
    }
    else {
        Write-Output ('扫描根尚未创建（目标骨架由 WP-01-T02 交付）: ' + $defaultRoot)
    }
}

# ---- 文件收集（扫描根缺失时为空集，R7 脚本规则仍然执行）----
$allFiles = @()
$scannedCount = 0
if ($null -ne $root) {
    $allFiles = @(Get-ChildItem -LiteralPath $root -Recurse -File)
    $scannedCount = $allFiles.Count
}
$headerFiles = @($allFiles | Where-Object { $_.Extension -in '.h', '.hpp' })
$sourceFiles = @($allFiles | Where-Object { $_.Extension -in '.cpp', '.cc', '.cxx' })
$cmakeFiles = @($allFiles | Where-Object { $_.Name -eq 'CMakeLists.txt' -or $_.Extension -eq '.cmake' })

$oldPluginNamePattern = '^sdurws_(robotmodelbuilder|engineeringrequirements|kinematicanalysis|structureoptimizer)'
$registeredTargetPattern = '^(sdurws_ird_[a-z0-9]+(_test)?|Qt[56]::[A-Za-z0-9]+|rw_[A-Za-z0-9_]+|sdurws|RobWork[A-Za-z0-9]*|Threads::Threads)$'
$linkKeywordPattern = '^(PUBLIC|PRIVATE|INTERFACE|DEBUG|OPTIMIZED|GENERAL|LINK_PUBLIC|LINK_PRIVATE|LINK_INTERFACE_LIBRARIES)$'

# ---- R1：核心公共头包含 QWidget/QApplication/QtWidgets/旧插件头 ----
foreach ($header in $headerFiles) {
    $relative = $header.FullName.Substring($root.Length).TrimStart('\')
    if (-not (Test-CoreHeaderPath -RelativePath $relative)) { continue }
    $lines = Read-TextLines -Path $header.FullName
    if ($null -eq $lines) { continue }
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match '#include\s*[<"]\s*(QWidget|QApplication)\s*[>"]' -or
            $lines[$i] -match '#include\s*[<"]\s*QtWidgets/') {
            Add-BoundaryViolation -Rule 'R1 QWidget/QApplication 头包含' -File $relative -Line ($i + 1) `
                -Detail ('核心公共头包含 Qt Widgets 头：' + $lines[$i].Trim()) `
                -Fix '核心模块仅允许 Qt Core/Gui 中非 Widget 部分（WP-01 §2.2）；将 Widget 依赖移至 sdurws_ird_ui。'
        }
        if ($lines[$i] -match '#include\s*[<"][^>"]*(robotmodelbuilder|engineeringrequirements|kinematicanalysis|structureoptimizer)') {
            Add-BoundaryViolation -Rule 'R1 旧插件头包含' -File $relative -Line ($i + 1) `
                -Detail ('核心公共头包含旧插件头：' + $lines[$i].Trim()) `
                -Fix '核心只经 public-interfaces.md §6.3 稳定端口协作；删除旧插件头包含并改用登记端口。'
        }
    }
}

# ---- R2/R3：CMake 链接旧插件目标或未登记 target ----
foreach ($cmake in $cmakeFiles) {
    $relative = $cmake.FullName.Substring($root.Length).TrimStart('\')
    $lines = Read-TextLines -Path $cmake.FullName
    if ($null -eq $lines) { continue }
    foreach ($block in (Get-CMakeCommandBlocks -Lines $lines -Command 'target_link_libraries')) {
        $blockLines = $block.Text -split "`n"
        $firstTokenSkipped = $false
        for ($b = 0; $b -lt $blockLines.Count; $b++) {
            $lineNumber = $block.Start + $b + 1
            $tokens = @($blockLines[$b] -replace '#.*$', '' -replace '//.*$', '' -split '\s+' | Where-Object { $_ -ne '' })
            foreach ($token in $tokens) {
                # 行尾 ')' 与最后一个依赖 token 粘连，剥离括号而非跳过整个 token。
                $t = $token.Trim('"').Trim('(').Trim(')')
                if ($t -eq '') { continue }
                if ($t -match $linkKeywordPattern) { continue }
                if ($t -match '^(target_link_libraries)$') { continue }
                if (-not $firstTokenSkipped) { $firstTokenSkipped = $true; continue }
                if ($t -match $oldPluginNamePattern) {
                    Add-BoundaryViolation -Rule 'R2 旧插件目标依赖' -File $relative -Line $lineNumber `
                        -Detail ('核心链接旧插件目标：' + $t) `
                        -Fix '删除对旧插件目标的链接，改用 sdurws_ird_* 拓扑内依赖（WP-01 §2.1）。'
                }
                elseif ($t -notmatch $registeredTargetPattern) {
                    Add-BoundaryViolation -Rule 'R3 未登记 target' -File $relative -Line $lineNumber `
                        -Detail ('链接未登记 target：' + $t) `
                        -Fix '仅允许 §2.1 拓扑目标；新依赖走 WP-01 §8 基线审批后登记。'
                }
            }
        }
    }
}

# ---- R4/R5：业务插件运行时名称拼接与碰撞默认值声明 ----
$pluginFiles = @($headerFiles + $sourceFiles | Where-Object { $_.FullName.Substring($root.Length) -match '(^|\\)plugins(\\|$)' })
foreach ($file in $pluginFiles) {
    $relative = $file.FullName.Substring($root.Length).TrimStart('\')
    $lines = Read-TextLines -Path $file.FullName
    if ($null -eq $lines) { continue }
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match '\+\s*"\."' -or $lines[$i] -match "\+\s*'\.'" -or $lines[$i] -match '"%1\.%2"') {
            Add-BoundaryViolation -Rule 'R4 运行时名称拼接' -File $relative -Line ($i + 1) `
                -Detail ('业务插件自行拼接运行时名称：' + $lines[$i].Trim()) `
                -Fix ('名称仅由 runtime 模块 RuntimeNameMap/IRuntimeNameResolver 生成与反解（public-interfaces §6.3）；改用解析器端口。')
        }
        if ($lines[$i] -match '(?i)\bSafetyDistance\b|\bcollisionMargin\b|(?i)ProximitySetup\s*\(|(?i)DefaultCollision') {
            Add-BoundaryViolation -Rule 'R5 碰撞默认值声明' -File $relative -Line ($i + 1) `
                -Detail ('业务插件声明碰撞默认值/安全距离：' + $lines[$i].Trim()) `
                -Fix '碰撞默认值唯一所有者为 sdurws_ird_policy（WP-01 §2.2）；插件经共享策略端口读取。'
        }
    }
}

# ---- R6：安装规则含测试数据/私有头/绝对构建路径 ----
foreach ($cmake in $cmakeFiles) {
    $relative = $cmake.FullName.Substring($root.Length).TrimStart('\')
    $lines = Read-TextLines -Path $cmake.FullName
    if ($null -eq $lines) { continue }
    foreach ($block in (Get-CMakeCommandBlocks -Lines $lines -Command 'install')) {
        $blockLines = $block.Text -split "`n"
        for ($b = 0; $b -lt $blockLines.Count; $b++) {
            $lineNumber = $block.Start + $b + 1
            if ($blockLines[$b] -match '(?i)testdata') {
                Add-BoundaryViolation -Rule 'R6 安装规则违规' -File $relative -Line $lineNumber `
                    -Detail ('安装规则包含测试数据：' + $blockLines[$b].Trim()) `
                    -Fix '测试数据不随产品安装（testing-contract）；从 install 规则移除 testdata 路径。'
            }
            if ($blockLines[$b] -match '_p\.hpp') {
                Add-BoundaryViolation -Rule 'R6 安装规则违规' -File $relative -Line $lineNumber `
                    -Detail ('安装规则包含私有头：' + $blockLines[$b].Trim()) `
                    -Fix '私有头（*_p.hpp）不进入安装清单；仅安装 public-interfaces 登记的公共头。'
            }
            if ($blockLines[$b] -match '[A-Za-z]:[\\/]') {
                Add-BoundaryViolation -Rule 'R6 安装规则违规' -File $relative -Line $lineNumber `
                    -Detail ('安装规则包含绝对构建路径：' + $blockLines[$b].Trim()) `
                    -Fix '安装目标必须使用相对路径与 CMake 目标导出；移除盘符绝对路径。'
            }
        }
    }
}

# ---- R7：脚本虚拟平台设置或 GUI 并行启动（恒扫描本脚本目录，与 -ScanRoot 无关）----
# 匹配串按拼接构造，避免扫描器源码自命中。
# 匹配串按拼接构造且变量名避开禁词原文，防止扫描器源码被 R7 自命中。
$virtualPlatformToken = 'of' + 'fscreen'
$guiParallelToken = 'Start-' + 'Job|Start-' + 'ThreadJob|-Paral' + 'lel\b'
$scriptFiles = @(Get-ChildItem -LiteralPath $PSScriptRoot -Filter '*.ps1' -File)
foreach ($script in $scriptFiles) {
    $lines = Read-TextLines -Path $script.FullName
    if ($null -eq $lines) { continue }
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match $virtualPlatformToken) {
            Add-BoundaryViolation -Rule 'R7 脚本 GUI 规则违规' -File $script.Name -Line ($i + 1) `
                -Detail ('脚本包含虚拟平台（非 windows）设置：' + $lines[$i].Trim()) `
                -Fix 'Windows Qt 测试平台固定为 windows（testing-contract）；删除虚拟平台设置。'
        }
        if ($lines[$i] -match $guiParallelToken) {
            Add-BoundaryViolation -Rule 'R7 脚本 GUI 规则违规' -File $script.Name -Line ($i + 1) `
                -Detail ('脚本包含 GUI 并行启动逻辑：' + $lines[$i].Trim()) `
                -Fix 'GUI 测试一次只启动一个绝对路径可执行文件（testing-contract）；改用串行调度。'
        }
    }
}

# ---- 结果输出 ----
if ($violations.Count -gt 0) {
    foreach ($violation in $violations) {
        [Console]::Error.WriteLine($violation)
    }
    [Console]::Error.WriteLine(('Boundary scan FAILED: ' + $violations.Count + ' violation(s), root=' + $root))
    exit 1
}

$rootText = if ($null -ne $root) { $root } else { '<default-root-missing>' }
Write-Output ('Boundary scan passed: ' + $scannedCount + ' files scanned, root=' + $rootText)
exit 0
