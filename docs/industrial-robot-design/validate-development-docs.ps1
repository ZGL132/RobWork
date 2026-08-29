$ErrorActionPreference = 'Stop'

$requirementsPath = Join-Path $PSScriptRoot 'requirements.md'
$baselinePath = Join-Path $PSScriptRoot 'DOCUMENT-BASELINE.md'
$masterPlanPath = Join-Path $PSScriptRoot 'development-task-breakdown.md'
$tracePath = Join-Path $PSScriptRoot 'requirement-traceability.csv'
$generatorPath = Join-Path $PSScriptRoot 'generate-traceability.ps1'
$benchmarkManifestPath = Join-Path $PSScriptRoot 'benchmark-manifest.json'
$workPackagePath = Join-Path $PSScriptRoot 'work-packages'
$architecturePath = Join-Path $PSScriptRoot 'architecture'
$agentTaskPath = Join-Path $PSScriptRoot 'agent-tasks'
$moduleDesignPath = Join-Path $PSScriptRoot 'module-design'

$errors = [System.Collections.Generic.List[string]]::new()

function Add-ValidationError {
    param([string]$Message)
    $script:errors.Add($Message)
}

function Test-MarkdownTables {
    param([string]$Path)

    $lines = Get-Content -LiteralPath $Path -Encoding UTF8
    $insideTable = $false
    $expectedPipes = 0

    for ($index = 0; $index -lt $lines.Count; $index++) {
        $line = $lines[$index]
        if ($line -match '^\s*\|') {
            # 单元格内转义竖线 \| 不是列分隔符，计数前剔除。
            $pipeCount = ($line.ToCharArray() | Where-Object { $_ -eq '|' }).Count - ([regex]::Matches($line, '\\\|')).Count
            if (-not $insideTable) {
                $insideTable = $true
                $expectedPipes = $pipeCount
            }
            if ($pipeCount -ne $expectedPipes) {
                Add-ValidationError "$Path line $($index + 1) has $pipeCount table separators; expected $expectedPipes."
            }
        }
        else {
            $insideTable = $false
            $expectedPipes = 0
        }
    }
}

foreach ($requiredPath in @($requirementsPath, $baselinePath, $masterPlanPath, $tracePath, $generatorPath, $benchmarkManifestPath, $workPackagePath, $architecturePath, $agentTaskPath, $moduleDesignPath)) {
    if (-not (Test-Path -LiteralPath $requiredPath)) {
        Add-ValidationError "Missing required path: $requiredPath"
    }
}

if ($errors.Count -eq 0) {
    try {
        $benchmark = Get-Content -LiteralPath $benchmarkManifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
        foreach ($field in @('schemaVersion', 'datasetVersion', 'threadCounts', 'randomSeed', 'measurementRuns', 'meshTriangles', 'backgroundLoad')) {
            if ($null -eq $benchmark.$field) {
                Add-ValidationError "Benchmark manifest is missing $field."
            }
        }
    }
    catch {
        Add-ValidationError "Benchmark manifest is not valid JSON: $benchmarkManifestPath"
    }

    if ($errors.Count -gt 0) {
        $errors | ForEach-Object { Write-Error $_ }
        exit 1
    }

    $requirementsText = Get-Content -LiteralPath $requirementsPath -Raw -Encoding UTF8
    $requirementMatches = [regex]::Matches(
        $requirementsText,
        '(?m)^\|\s*([A-Z][A-Z0-9-]*-\d+)\s*\|\s*(P[01])\s*\|'
    )
    $requirementIds = @($requirementMatches | ForEach-Object { $_.Groups[1].Value })
    $p0Count = @($requirementMatches | Where-Object { $_.Groups[2].Value -eq 'P0' }).Count
    $p1Count = @($requirementMatches | Where-Object { $_.Groups[2].Value -eq 'P1' }).Count
    $acceptanceIds = @(
        [regex]::Matches($requirementsText, '(?m)^\|\s*(AT-\d+)\s*\|') |
            ForEach-Object { $_.Groups[1].Value }
    )

    if ($requirementIds.Count -ne 124) {
        Add-ValidationError "Expected 124 requirement rows; found $($requirementIds.Count)."
    }
    if (@($requirementIds | Sort-Object -Unique).Count -ne 124) {
        Add-ValidationError 'Requirement IDs are not unique.'
    }
    if ($p0Count -ne 110 -or $p1Count -ne 14) {
        Add-ValidationError "Expected 110 P0 and 14 P1 requirements; found $p0Count P0 and $p1Count P1."
    }
    if ($acceptanceIds.Count -ne 19 -or @($acceptanceIds | Sort-Object -Unique).Count -ne 19) {
        Add-ValidationError 'Acceptance-test IDs must contain 19 unique rows.'
    }

    $traceRows = @(Import-Csv -LiteralPath $tracePath)
    $requiredColumns = @(
        'requirement_id', 'priority', 'requirement_summary', 'work_package',
        'implementation_task', 'test_task', 'review_task', 'acceptance_scenario', 'phase', 'release', 'status'
    )

    if ($traceRows.Count -ne 124) {
        Add-ValidationError "Expected 124 trace rows; found $($traceRows.Count)."
    }
    foreach ($column in $requiredColumns) {
        if ($traceRows.Count -gt 0 -and $column -notin $traceRows[0].PSObject.Properties.Name) {
            Add-ValidationError "Traceability CSV is missing column $column."
        }
    }

    $traceIds = @($traceRows.requirement_id)
    $missingTrace = @($requirementIds | Where-Object { $_ -notin $traceIds })
    $extraTrace = @($traceIds | Where-Object { $_ -notin $requirementIds })
    $duplicateTrace = @($traceIds | Group-Object | Where-Object Count -ne 1)

    if ($missingTrace.Count -gt 0) {
        Add-ValidationError "Requirements missing from trace: $($missingTrace -join ', ')."
    }
    if ($extraTrace.Count -gt 0) {
        Add-ValidationError "Unknown requirements in trace: $($extraTrace -join ', ')."
    }
    if ($duplicateTrace.Count -gt 0) {
        Add-ValidationError "Duplicate trace rows: $($duplicateTrace.Name -join ', ')."
    }

    # 阶段 B 验收的行必须至少有一个明确列入阶段 B 交付范围的工作包。
    # 使用显式集合，避免未来新增工作包后按编号意外获得阶段资格。
    $phaseBEligiblePackages = @(
        'WP-00', 'WP-01', 'WP-02', 'WP-03', 'WP-04', 'WP-05', 'WP-06',
        'WP-07', 'WP-08', 'WP-09', 'WP-10', 'WP-11', 'WP-12', 'WP-13',
        'WP-14', 'WP-15', 'WP-20', 'WP-23'
    )

    foreach ($row in $traceRows) {
        foreach ($column in $requiredColumns) {
            if ([string]::IsNullOrWhiteSpace($row.$column)) {
                Add-ValidationError "Trace row $($row.requirement_id) has an empty $column."
            }
        }
        foreach ($package in ($row.work_package -split ';')) {
            if ($package -notmatch '^WP-(0\d|1\d|2[0-5])$') {
                Add-ValidationError "Trace row $($row.requirement_id) references invalid work package $package."
            }
        }
        if ($row.release -notin @('R1', 'R2', 'R1/R2')) {
            Add-ValidationError "Trace row $($row.requirement_id) has invalid release $($row.release)."
        }
        if ($row.requirement_id -match '^OPT-(05|09|10)$' -and $row.release -ne 'R2') {
            Add-ValidationError "Trace row $($row.requirement_id) must be R2-only."
        }
        if ($row.requirement_id -match '^OPT-(01|02|03|04|06|07|08)$' -and $row.release -ne 'R1/R2') {
            Add-ValidationError "Trace row $($row.requirement_id) must be R1/R2."
        }
        if ($row.phase -match 'B') {
            $hasEarlyPackage = @(
                ($row.work_package -split ';') | Where-Object { $_ -in $phaseBEligiblePackages }
            ).Count -gt 0
            if (-not $hasEarlyPackage) {
                Add-ValidationError "Trace row $($row.requirement_id) has phase '$($row.phase)' but no work package deliverable by phase B."
            }
        }
    }

    $masterText = Get-Content -LiteralPath $masterPlanPath -Raw -Encoding UTF8
    $masterPackages = @(
        [regex]::Matches($masterText, '(?m)^\|\s*(WP-(?:0\d|1\d|2[0-5]))\s*\|') |
            ForEach-Object { $_.Groups[1].Value }
    )
    $expectedPackages = @(0..25 | ForEach-Object { 'WP-{0:D2}' -f $_ })
    $missingPackages = @($expectedPackages | Where-Object { $_ -notin $masterPackages })
    $duplicatePackages = @($masterPackages | Group-Object | Where-Object Count -ne 1)
    if ($missingPackages.Count -gt 0) {
        Add-ValidationError "Master plan is missing work packages: $($missingPackages -join ', ')."
    }
    if ($duplicatePackages.Count -gt 0) {
        Add-ValidationError "Master plan has duplicate work-package rows: $($duplicatePackages.Name -join ', ')."
    }

    $workPackageFiles = @(Get-ChildItem -LiteralPath $workPackagePath -File -Filter 'WP-*.md')
    foreach ($number in 0..25) {
        $prefix = 'WP-{0:D2}-' -f $number
        if (@($workPackageFiles | Where-Object { $_.Name.StartsWith($prefix) }).Count -ne 1) {
            Add-ValidationError "Expected exactly one detailed work-package plan beginning with $prefix."
        }
    }

    foreach ($workPackageFile in $workPackageFiles) {
        $workPackageText = Get-Content -LiteralPath $workPackageFile.FullName -Raw -Encoding UTF8
        foreach ($section in @('## 任务', '## 验证', '## 退出条件')) {
            if ($workPackageText -notmatch [regex]::Escape($section)) {
                Add-ValidationError "$($workPackageFile.Name) is missing required section $section."
            }
        }
        if ($workPackageText -notmatch '\*\*(目标|Goal)[:：]?\*\*') {
            Add-ValidationError "$($workPackageFile.Name) is missing a goal declaration."
        }
        if ($workPackageFile.Name -match '^WP-(1[3-9]|2[0-5])-') {
            foreach ($field in @('阶段/发布：', '需求与契约：', '拥有目录：', '输入/输出：')) {
                if ($workPackageText -notmatch [regex]::Escape($field)) {
                    Add-ValidationError "$($workPackageFile.Name) is missing required field $field."
                }
            }
        }
    }

    $requiredArchitectureFiles = @(
        (Join-Path $architecturePath 'README.md'),
        (Join-Path $architecturePath 'contract-registry.md'),
        (Join-Path $architecturePath 'symbol-registry.md'),
        (Join-Path $architecturePath 'domain-model.md'),
        (Join-Path $architecturePath 'canonical-kinematics.md'),
        (Join-Path $architecturePath 'evaluation-semantics.md'),
        (Join-Path $architecturePath 'execution-model.md'),
        (Join-Path $architecturePath 'candidate-compilation.md'),
        (Join-Path $architecturePath 'persistence-schema.md'),
        (Join-Path $architecturePath 'public-interfaces.md'),
        (Join-Path $architecturePath 'testing-contract.md'),
        (Join-Path $architecturePath 'adr\README.md'),
        (Join-Path $architecturePath 'adr\ADR-005-orthogonal-result-status-and-naming.md'),
        (Join-Path $PSScriptRoot 'schemas\validate-schemas.ps1')
    )
    foreach ($contractFile in $requiredArchitectureFiles) {
        if (-not (Test-Path -LiteralPath $contractFile)) {
            Add-ValidationError "Missing architecture contract: $contractFile"
        }
    }

    $requiredModuleDesignFiles = @(
        (Join-Path $moduleDesignPath 'README.md'),
        (Join-Path $moduleDesignPath 'TEMPLATE.md'),
        (Join-Path $moduleDesignPath 'core-domain.md'),
        (Join-Path $moduleDesignPath 'persistence.md'),
        (Join-Path $moduleDesignPath 'snapshot-result.md'),
        (Join-Path $moduleDesignPath 'runtime-model.md'),
        (Join-Path $moduleDesignPath 'policy-collision.md'),
        (Join-Path $moduleDesignPath 'execution-platform.md'),
        (Join-Path $moduleDesignPath 'diagnostics.md'),
        (Join-Path $moduleDesignPath 'session-ui.md'),
        (Join-Path $moduleDesignPath 'secure-io.md'),
        (Join-Path $moduleDesignPath 'reporting.md'),
        (Join-Path $moduleDesignPath 'robot-modeling.md'),
        (Join-Path $moduleDesignPath 'requirements-definition.md'),
        (Join-Path $moduleDesignPath 'kinematics.md'),
        (Join-Path $moduleDesignPath 'trajectory-planning.md'),
        (Join-Path $moduleDesignPath 'dynamics.md'),
        (Join-Path $moduleDesignPath 'drivetrain.md'),
        (Join-Path $moduleDesignPath 'device-selection.md'),
        (Join-Path $moduleDesignPath 'optimization.md'),
        (Join-Path $moduleDesignPath 'workflow-integration.md'),
        (Join-Path $moduleDesignPath 'system-quality.md'),
        (Join-Path $moduleDesignPath 'installation-release.md'),
        (Join-Path $moduleDesignPath 'pilot-delivery.md'),
        (Join-Path $moduleDesignPath 'testkit.md')
    )
    foreach ($moduleFile in $requiredModuleDesignFiles) {
        if (-not (Test-Path -LiteralPath $moduleFile)) {
            Add-ValidationError "Missing module design: $moduleFile"
        }
    }

    $taskCardFiles = @(Get-ChildItem -LiteralPath $agentTaskPath -File -Filter 'WP-*.md' | Where-Object { $_.Name -ne 'WP-TEMPLATE.md' })
    $taskIds = [System.Collections.Generic.List[string]]::new()
    foreach ($taskCard in $taskCardFiles) {
        if ($taskCard.Name -notmatch '^WP-(\d{2})-T(\d{2})-[a-z0-9-]+\.md$') {
            Add-ValidationError "Invalid task-card filename: $($taskCard.Name)"
            continue
        }
        $taskId = $taskCard.BaseName -replace '^((WP-\d{2})-T\d{2})-.*$', '$1'
        if ($taskIds.Contains($taskId)) {
            Add-ValidationError "Duplicate task card: $taskId"
        }
        else {
            $taskIds.Add($taskId)
        }
        $wpId = 'WP-' + $Matches[1]
        if ($wpId -notin $expectedPackages) {
            Add-ValidationError "Task card $taskId references unknown work package $wpId."
        }
        $taskText = Get-Content -LiteralPath $taskCard.FullName -Raw -Encoding UTF8
        # D7 16 字段任务卡结构；标签按文本匹配（冒号位置/括注不影响）；非代码任务卡允许三个字段的变体名。
        $commonMarkers = @(
            'Task ID / 需求 ID / ADR / 阶段',
            '基线 commit',
            '前置任务及必需工件',
            '允许创建/修改/删除的文件',
            '禁止修改的文件和公共接口',
            '修改前接口',
            '修改后接口',
            'diff 和禁止项检查',
            '证据工件',
            '提交格式',
            '停止与升级条件'
        )
        $variantPairs = @(
            @('实施步骤', '交付步骤'),
            @('RED 测试', '验证准备'),
            @('精确验证命令', '精确验证方式')
        )
        foreach ($marker in $commonMarkers) {
            if ($taskText -notmatch [regex]::Escape($marker)) {
                Add-ValidationError "Task card $($taskCard.Name) is missing $marker."
            }
        }
        foreach ($pair in $variantPairs) {
            $hasAny = $false
            foreach ($marker in $pair) {
                if ($taskText.Contains($marker)) { $hasAny = $true }
            }
            if (-not $hasAny) {
                Add-ValidationError "Task card $($taskCard.Name) is missing $($pair[0]) (or variant $($pair[1]))."
            }
        }
        if ($taskText -notmatch 'architecture/') {
            Add-ValidationError "Task card $($taskCard.Name) does not reference an architecture contract."
        }
        $wpFile = @($workPackageFiles | Where-Object { $_.Name -match "^$wpId-" })
        if ($wpFile.Count -eq 1) {
            $wpText = Get-Content -LiteralPath $wpFile[0].FullName -Raw -Encoding UTF8
            if ($wpText -notmatch [regex]::Escape($taskId)) {
                Add-ValidationError "Task card $taskId is orphaned; no matching task in $($wpFile[0].Name)."
            }
        }
    }

    # 每个工作包正文声明的稳定任务 ID 都必须有且仅有一张独立任务卡。
    foreach ($workPackageFile in $workPackageFiles) {
        $workPackageText = Get-Content -LiteralPath $workPackageFile.FullName -Raw -Encoding UTF8
        $declaredTaskIds = @(
            [regex]::Matches($workPackageText, '(?m)\b(WP-\d{2}-T\d{2})\b') |
                ForEach-Object { $_.Groups[1].Value } |
                Sort-Object -Unique
        )
        foreach ($declaredTaskId in $declaredTaskIds) {
            if ($declaredTaskId -notin $taskIds) {
                Add-ValidationError "Work package $($workPackageFile.Name) declares $declaredTaskId without an independent task card."
            }
        }
    }

    $contractDocuments = @($requiredArchitectureFiles | Where-Object { Test-Path -LiteralPath $_ })
    $taskDocuments = @($taskCardFiles.FullName)

    $moduleDocuments = @($requiredModuleDesignFiles | Where-Object { Test-Path -LiteralPath $_ })
    $documents = @($requirementsPath, $masterPlanPath) + @($workPackageFiles.FullName) + $contractDocuments + $moduleDocuments + $taskDocuments
    foreach ($document in $documents) {
        Test-MarkdownTables -Path $document
        $documentText = Get-Content -LiteralPath $document -Raw -Encoding UTF8
        if ($documentText -match '(?i)\bTODO\b|\bTBD\b|PLACEHOLDER|\[To be written\]|待补充|待定') {
            Add-ValidationError "Placeholder content found in $document."
        }
        if ($documentText.Contains([char]0xFFFD)) {
            Add-ValidationError "Unicode replacement character found in $document."
        }
    }

    $temporaryTrace = Join-Path ([System.IO.Path]::GetTempPath()) ("ird-trace-" + [guid]::NewGuid() + '.csv')
    try {
        & $generatorPath -RequirementsPath $requirementsPath -OutputPath $temporaryTrace | Out-Null
        $expectedBytes = [System.IO.File]::ReadAllBytes($temporaryTrace)
        $actualBytes = [System.IO.File]::ReadAllBytes($tracePath)
        # 兼容 Windows PowerShell 5.1：此处不使用 PowerShell 7 的泛型方法调用语法。
        $bytesEqual = ($expectedBytes.Length -eq $actualBytes.Length)
        if ($bytesEqual) {
            for ($i = 0; $i -lt $expectedBytes.Length; $i++) {
                if ($expectedBytes[$i] -ne $actualBytes[$i]) {
                    $bytesEqual = $false
                    break
                }
            }
        }
        if (-not $bytesEqual) {
            Add-ValidationError 'Traceability CSV is stale; regenerate it from the requirements document.'
        }
    }
    finally {
        if (Test-Path -LiteralPath $temporaryTrace) {
            Remove-Item -LiteralPath $temporaryTrace -Force
        }
    }
}

if ($errors.Count -gt 0) {
    $errors | ForEach-Object { Write-Error $_ }
    exit 1
}

# 计数从注册表实际内容派生，避免硬编码数字随文档演进失真（PS 5.1 兼容）。
$contractRegistryText = Get-Content -LiteralPath (Join-Path $architecturePath 'contract-registry.md') -Raw -Encoding UTF8
$symbolRegistryText = Get-Content -LiteralPath (Join-Path $architecturePath 'symbol-registry.md') -Raw -Encoding UTF8
$contractCount = ([regex]::Matches($contractRegistryText, '(?m)^\| `CTR-')).Count
$symbolCount = ([regex]::Matches($symbolRegistryText, '(?m)^\| `SYM-')).Count
$adrCount = @(Get-ChildItem -LiteralPath (Join-Path $architecturePath 'adr') -Filter 'ADR-*.md' -File).Count
Write-Output "124 requirements, 19 acceptance tests, $contractCount contracts, $symbolCount symbols, $adrCount ADRs, 0 trace gaps"
