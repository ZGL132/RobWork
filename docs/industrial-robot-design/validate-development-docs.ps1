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

    if ($requirementIds.Count -ne 128) {
        Add-ValidationError "Expected 128 requirement rows; found $($requirementIds.Count)."
    }
    if (@($requirementIds | Sort-Object -Unique).Count -ne 128) {
        Add-ValidationError 'Requirement IDs are not unique.'
    }
    if ($p0Count -ne 114 -or $p1Count -ne 14) {
        Add-ValidationError "Expected 114 P0 and 14 P1 requirements; found $p0Count P0 and $p1Count P1."
    }
    if ($acceptanceIds.Count -ne 19 -or @($acceptanceIds | Sort-Object -Unique).Count -ne 19) {
        Add-ValidationError 'Acceptance-test IDs must contain 19 unique rows.'
    }
    $requirementTotal = @($requirementIds | Sort-Object -Unique).Count

    $traceRows = @(Import-Csv -LiteralPath $tracePath)
    $requiredColumns = @(
        'requirement_id', 'priority', 'requirement_summary', 'primary_wp',
        'supporting_wps', 'agent_task_ids', 'test_case_ids', 'acceptance_scenario',
        'evidence_artifact', 'release_gate', 'phase', 'release', 'status'
    )

    if ($traceRows.Count -ne $requirementTotal) {
        Add-ValidationError "Expected $requirementTotal trace rows; found $($traceRows.Count)."
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
        # D8 列结构：primary_wp 单值；supporting_wps 分号分隔，'-' 表示无支持工作包。
        $rowPackages = @($row.primary_wp) +
            @(($row.supporting_wps -split ';') | Where-Object { $_ -and $_ -ne '-' })
        foreach ($package in $rowPackages) {
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
                $rowPackages | Where-Object { $_ -in $phaseBEligiblePackages }
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

    # ---- D9 增强检查 ----

    # 1) 验证命令禁省略号（任务卡与工作包计划）。
    foreach ($cmdDoc in @($taskCardFiles.FullName) + @($workPackageFiles.FullName)) {
        $cmdText = Get-Content -LiteralPath $cmdDoc -Raw -Encoding UTF8
        if ($cmdText -match '(?m)^.*(-File |ctest |cmake --build)[^\r\n]*\.\.\.') {
            Add-ValidationError "Ellipsis in a verification command: $cmdDoc"
        }
    }

    # 2) ADR 文件存在且状态合法。
    $adrIndexPath = Join-Path $architecturePath 'adr\README.md'
    if (Test-Path -LiteralPath $adrIndexPath) {
        $adrIndexText = Get-Content -LiteralPath $adrIndexPath -Raw -Encoding UTF8
        foreach ($adrMatch in [regex]::Matches($adrIndexText, '\((ADR-\d{3}-[a-z0-9-]+)\.md\)[^|]*\|[^|]*\|[^|]*\|[^|]*\|\s*`([A-Za-z]+)`')) {
            $adrFile = Join-Path $architecturePath ('adr\' + $adrMatch.Groups[1].Value + '.md')
            if (-not (Test-Path -LiteralPath $adrFile)) {
                Add-ValidationError "ADR indexed but missing: $($adrMatch.Groups[1].Value)"
            }
            if ($adrMatch.Groups[2].Value -notin @('Proposed', 'Accepted', 'Superseded')) {
                Add-ValidationError "ADR $($adrMatch.Groups[1].Value) has invalid status $($adrMatch.Groups[2].Value)."
            }
        }
    }

    # 3) 公共符号异名一致性：裁决记录文件之外不得出现禁止名称。
    $adjudicationAllowlist = @(
        (Join-Path $PSScriptRoot 'README.md'),
        (Join-Path $PSScriptRoot 'DOCUMENT-BASELINE.md'),
        (Join-Path $architecturePath 'symbol-registry.md'),
        (Join-Path $architecturePath 'contract-registry.md')
    )
    $adjudicationAllowlist += @(Get-ChildItem -LiteralPath (Join-Path $architecturePath 'adr') -Filter '*.md' -File | ForEach-Object { $_.FullName })
    $bannedAliasDocs = @($workPackageFiles.FullName) + @($moduleDocuments) + @($taskCardFiles.FullName) + @($contractDocuments)
    foreach ($aliasDoc in $bannedAliasDocs) {
        if ($adjudicationAllowlist -contains $aliasDoc) { continue }
        # 行级判断：禁名出现在"禁止/禁名/零命中/静态扫描"等裁决语境行是合法指令（grep 目标），
        # 出现在无语境行视为把禁名当符号使用。
        $contextPattern = '(禁止|禁名|禁用|零命中|旧名|静态扫描|static scan|不得|禁止名称)'
        foreach ($line in (Get-Content -LiteralPath $aliasDoc -Encoding UTF8)) {
            $hasContext = $line -match $contextPattern
            foreach ($bannedAlias in @('EvaluationEnvelope', 'DynamicsResult', 'CandidateResult')) {
                if ($line.Contains($bannedAlias) -and -not $hasContext) {
                    Add-ValidationError "Banned alias $bannedAlias used without adjudication context in $aliasDoc (see symbol-registry §4)."
                }
            }
            if ($line -match 'AnalysisConfig\b' -and -not $hasContext) {
                Add-ValidationError "Banned alias AnalysisConfig used without adjudication context in $aliasDoc (use AnalysisConfiguration)."
            }
            if ($line -match 'OptimizationStudy\b' -and -not $hasContext) {
                Add-ValidationError "Banned alias OptimizationStudy used without adjudication context in $aliasDoc (use OptimizationStudyDefinition/OptimizationRunResult)."
            }
        }
    }

    # 4) 代码基线 commit 必须存在于当前仓库。
    $baselineText = Get-Content -LiteralPath $baselinePath -Raw -Encoding UTF8
    $baselineMatch = [regex]::Match($baselineText, '\| 代码基线 \| `([0-9a-f]{40})`')
    if ($baselineMatch.Success) {
        $baselineSha = $baselineMatch.Groups[1].Value
        $repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
        Push-Location $repoRoot
        try {
            & git cat-file -e $baselineSha 2>$null
            if ($LASTEXITCODE -ne 0) {
                Add-ValidationError "Code baseline commit $baselineSha not found in repository."
            }
        }
        finally {
            Pop-Location
        }
    }

    # 5) 工作包依赖图无环（总纲 §5 前置列 → 边 → DFS 找环）。
    $masterPlanText = Get-Content -LiteralPath $masterPlanPath -Raw -Encoding UTF8
    $wpDependencies = @{}
    foreach ($rowMatch in [regex]::Matches($masterPlanText, '(?m)^\| (WP-\d{2}) \|[^|]*\|[^|]*\|[^|]*\|([^|]*)\|')) {
        $wpId = $rowMatch.Groups[1].Value
        $prereqCell = $rowMatch.Groups[2].Value
        $prereqs = [System.Collections.Generic.List[string]]::new()
        foreach ($depMatch in [regex]::Matches($prereqCell, '\b(WP-\d{2})(?:～(\d{2}))?\b')) {
            $prereqs.Add($depMatch.Groups[1].Value)
            if ($depMatch.Groups[2].Success) {
                for ($n = [int]$depMatch.Groups[1].Value.Substring(3) + 1; $n -le [int]$depMatch.Groups[2].Value; $n++) {
                    $prereqs.Add('WP-{0:D2}' -f $n)
                }
            }
        }
        $wpDependencies[$wpId] = $prereqs
    }
    $visitState = @{}
    $cycleFound = $null
    function Test-WpCycle {
        param([string]$Node)
        if ($visitState[$Node] -eq 1) { $script:cycleFound = $Node; return $true }
        if ($visitState[$Node] -eq 2) { return $false }
        $visitState[$Node] = 1
        if ($script:wpDependencies.ContainsKey($Node)) {
            foreach ($next in $script:wpDependencies[$Node]) {
                if (Test-WpCycle -Node $next) { return $true }
            }
        }
        $visitState[$Node] = 2
        return $false
    }
    foreach ($wpNode in @($wpDependencies.Keys)) {
        if (Test-WpCycle -Node $wpNode) { break }
    }
    if ($null -ne $cycleFound) {
        Add-ValidationError "Work package dependency cycle detected through $cycleFound."
    }

    # 6) 公共接口定义所有权：值对象/端口正文定义只能出现在 architecture/public-interfaces.md。
    $ownedDefinitions = @(
        'struct ResultEnvelope', 'struct AnalysisSnapshot', 'struct EvaluatorInputSlice',
        'struct EvidenceBundle', 'class IProjectCommandService', 'class IProjectQuery',
        'class IResultRepository', 'class IEngineeringEvaluator', 'class IRuntimeNameResolver',
        'class IEvaluationScheduler'
    )
    $consumerDocs = @($moduleDocuments) + @($workPackageFiles.FullName)
    foreach ($consumerDoc in $consumerDocs) {
        $consumerText = Get-Content -LiteralPath $consumerDoc -Raw -Encoding UTF8
        foreach ($ownedDef in $ownedDefinitions) {
            if ($consumerText.Contains($ownedDef)) {
                Add-ValidationError "Public definition '$ownedDef' must live in architecture/public-interfaces.md, found in $consumerDoc."
            }
        }
    }

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

# ---------------------------------------------------------------- D12 语义/执行/治理闭合检查

# 1) 任务级 DAG：前置引用存在性 + 无环
$cardTaskIds = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
$taskPrereqs = @{}
$cardFilePaths = @{}
$cardFilesForDag = @(Get-ChildItem -LiteralPath $agentTaskPath -Filter 'WP-*.md' -File)
foreach ($cf in $cardFilesForDag) {
    if ($cf.BaseName -match '^(WP-\d{2}-T\d{2})') {
        $selfId = $Matches[1]
        [void]$cardTaskIds.Add($selfId)
        $cardFilePaths[$selfId] = $cf.FullName
        $raw = Get-Content -LiteralPath $cf.FullName -Raw -Encoding UTF8
        $prereqField = [regex]::Match($raw, '(?m)^- \*\*前置任务及必需工件[^\r\n]*')
        # 剥离方向性表述（"先于 X 执行"/"不依赖 X"/"装配归 X"等非依赖提法），避免假依赖边
        $fieldText = $prereqField.Value -replace '(本任务)?先于[^。；]*', '' -replace '不依赖[^。；]*', '' -replace '(归|移交) WP-\d{2}-T\d{2}', ''
        $prereqIds = @([regex]::Matches($fieldText, 'WP-\d{2}-T\d{2}') | ForEach-Object { $_.Value } | Where-Object { $_ -ne $selfId } | Sort-Object -Unique)
        $taskPrereqs[$selfId] = $prereqIds
    }
}
foreach ($t0 in @($cardTaskIds)) {
    foreach ($pid2 in $taskPrereqs[$t0]) {
        if (-not $cardTaskIds.Contains($pid2)) {
            Add-ValidationError "Task card $t0 references prerequisite $pid2 which has no task card."
        }
    }
}
$dagState = @{}
function Test-TaskDag {
    param([string]$NodeId)
    if ($dagState.ContainsKey($NodeId)) {
        return ($dagState[$NodeId] -eq 2)
    }
    $dagState[$NodeId] = 1
    foreach ($p in $taskPrereqs[$NodeId]) {
        if (-not (Test-TaskDag -NodeId $p)) {
            return $false
        }
    }
    $dagState[$NodeId] = 2
    return $true
}
foreach ($t in @($cardTaskIds)) {
    if (-not (Test-TaskDag -NodeId $t)) {
        Add-ValidationError "Task-level dependency cycle detected involving $t (check 前置任务 fields)."
        break
    }
}

# 2) 诊断注册表一致性：登记表为唯一权威（diagnostics.md §3）
$diagReg = @{}
$diagText = Get-Content -LiteralPath (Join-Path $moduleDesignPath 'diagnostics.md') -Raw -Encoding UTF8
foreach ($line in ($diagText -split "`r?`n")) {
    if ($line -match '^\|\s*(IRD-[A-Z0-9-]+)\s*\|\s*[^|]+\|\s*(Input|Engineering|System)\s*\|\s*(Info|Warning|Error)\s*\|') {
        if (-not $diagReg.ContainsKey($Matches[1])) {
            $diagReg[$Matches[1]] = @{ Category = $Matches[2]; Severity = $Matches[3] }
        }
    }
}
$diagScanDirs = @($moduleDesignPath, $workPackagePath, $agentTaskPath, $architecturePath)
$diagCodePattern = 'IRD-[A-Z0-9]+(?:-[A-Z0-9]+)+'
$diagCatPattern = '\b(Input|Engineering|System)\b'
$diagSevPattern = '\b(Info|Warning|Error)\b'
foreach ($dd in $diagScanDirs) {
    foreach ($df in (Get-ChildItem -LiteralPath $dd -Filter '*.md' -Recurse -File)) {
        if ($df.Name -eq 'diagnostics.md' -and $df.DirectoryName -eq $moduleDesignPath) { continue }
        $dLines = Get-Content -LiteralPath $df.FullName -Encoding UTF8
        for ($i = 0; $i -lt $dLines.Count; $i++) {
            $dLine = $dLines[$i]
            $dMatches = [regex]::Matches($dLine, $diagCodePattern)
            for ($m = 0; $m -lt $dMatches.Count; $m++) {
                $dCode = $dMatches[$m].Value
                if ($dCode -match '^IRD-D\d-') { continue }
                $rest = $dLine.Substring($dMatches[$m].Index + $dMatches[$m].Length)
                if ($rest.StartsWith('-*')) { continue }
                $wEnd = $dLine.Length
                if ($m -lt $dMatches.Count - 1) { $wEnd = $dMatches[$m + 1].Index }
                $window = $dLine.Substring($dMatches[$m].Index + $dMatches[$m].Length, $wEnd - $dMatches[$m].Index - $dMatches[$m].Length)
                if ($window -match '[A-Z0-9]') { continue }
                $dCat = @([regex]::Matches($window, $diagCatPattern) | ForEach-Object { $_.Value } | Select-Object -Unique)
                $dSev = @([regex]::Matches($window, $diagSevPattern) | ForEach-Object { $_.Value } | Select-Object -Unique)
                if ($dCat.Count -eq 0 -or $dSev.Count -eq 0) { continue }
                if (-not $diagReg.ContainsKey($dCode)) {
                    Add-ValidationError ("Unregistered diagnostic code '{0}' with category/severity claim at {1}:{2}." -f $dCode, $df.Name, ($i + 1))
                    continue
                }
                if (($dCat[0] -ne $diagReg[$dCode].Category) -or ($dSev[0] -ne $diagReg[$dCode].Severity)) {
                    Add-ValidationError ("Diagnostic '{0}' claim {1}/{2} at {3}:{4} conflicts with registry {5}/{6}." -f $dCode, $dCat[0], $dSev[0], $df.Name, ($i + 1), $diagReg[$dCode].Category, $diagReg[$dCode].Severity)
                }
            }
        }
    }
}

# 3) 公共符号：注册表唯一 + 跨文档引用存在
$symbolRegistryText = Get-Content -LiteralPath (Join-Path $architecturePath 'symbol-registry.md') -Raw -Encoding UTF8
$symbolIds = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
foreach ($line in ($symbolRegistryText -split "`r?`n")) {
    if ($line -match '^\|\s*`(SYM-[A-Z]+-\d+)`') {
        if (-not $symbolIds.Add($Matches[1])) {
            Add-ValidationError "Duplicate symbol ID in symbol-registry.md: $($Matches[1])."
        }
    }
}
foreach ($dd in @($moduleDesignPath, $workPackagePath, $agentTaskPath, $architecturePath)) {
    foreach ($sf in (Get-ChildItem -LiteralPath $dd -Filter '*.md' -Recurse -File)) {
        $sText = Get-Content -LiteralPath $sf.FullName -Raw -Encoding UTF8
        foreach ($sm in [regex]::Matches($sText, 'SYM-[A-Z]+-\d+')) {
            if (-not $symbolIds.Contains($sm.Value)) {
                Add-ValidationError "Unregistered symbol reference '$($sm.Value)' in $($sf.Name)."
            }
        }
    }
}

# 4) 反向追踪：每张任务卡必须出现在需求追踪表或治理追踪表
$tracedTaskIds = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
foreach ($row in (Import-Csv -LiteralPath $tracePath)) {
    foreach ($tid in ($row.agent_task_ids -split ';')) {
        if ($tid.Trim()) { [void]$tracedTaskIds.Add($tid.Trim()) }
    }
}
$governanceTracePath = Join-Path $PSScriptRoot 'governance-traceability.csv'
if (-not (Test-Path -LiteralPath $governanceTracePath)) {
    Add-ValidationError 'governance-traceability.csv is missing; regenerate it with generate-traceability.ps1.'
} else {
    foreach ($row in (Import-Csv -LiteralPath $governanceTracePath)) {
        if ($row.task_id) { [void]$tracedTaskIds.Add($row.task_id) }
    }
}
foreach ($t in @($cardTaskIds)) {
    if (-not $tracedTaskIds.Contains($t)) {
        Add-ValidationError "Task card $t is not covered by requirement-traceability.csv or governance-traceability.csv."
    }
}

# 5) 证据路径：任务卡与追踪表统一使用 out/test-evidence/wp-xx/<run-id>/
foreach ($cf in $cardFilesForDag) {
    $raw = Get-Content -LiteralPath $cf.FullName -Raw -Encoding UTF8
    if ($raw -cmatch 'evidence/WP-\d|(?<!out/test-)evidence/wp-\d') {
        Add-ValidationError "Task card $($cf.Name) still references the legacy evidence/ path; use out/test-evidence/wp-xx/<run-id>/."
    }
}
foreach ($row in (Import-Csv -LiteralPath $tracePath)) {
    if ($row.evidence_artifact -and -not $row.evidence_artifact.StartsWith('out/test-evidence/')) {
        Add-ValidationError "Traceability evidence path for $($row.requirement_id) must start with out/test-evidence/."
    }
}

# 6) 命令禁项：grep / 任选其一 / 自然语言占位路径
# 内建夹具自检（防检测正则假阴性回归）：夹具必须命中、rg 不得误报。
function Test-GrepBan {
    param([string]$Text)
    return ($Text -match '(^|[^a-zA-Z.])grep(\.exe)?(\s|$)')
}
if (-not (Test-GrepBan -Text '  - `grep -rn "foo" src/`')) {
    Add-ValidationError '内置夹具失败：grep -rn 未被禁项检测识别。'
}
if (-not (Test-GrepBan -Text 'grep.exe -R "foo" .')) {
    Add-ValidationError '内置夹具失败：grep.exe -R 未被禁项检测识别。'
}
if (-not (Test-GrepBan -Text '  rg -ni "foo" src/ # 不应回退到 grep')) {
    Add-ValidationError '内置夹具失败：包含 grep 的注释行未被检测。'
}
if (Test-GrepBan -Text '  - 回退：`rg -n "foo" src/`') {
    Add-ValidationError '内置夹具失败：rg 误报为 grep。'
}
foreach ($cf in $cardFilesForDag) {
    $cLines = Get-Content -LiteralPath $cf.FullName -Encoding UTF8
    for ($i = 0; $i -lt $cLines.Count; $i++) {
        if (Test-GrepBan -Text $cLines[$i]) {
            Add-ValidationError "Task card $($cf.Name):$($i + 1) uses grep; use rg (executable on this Windows environment)."
        }
        if ($cLines[$i] -match '任选其一') {
            Add-ValidationError "Task card $($cf.Name):$($i + 1) uses 任选其一; commands must be 必执行 + named fallback order."
        }
        if ($cLines[$i] -match '业务插件目录') {
            Add-ValidationError "Task card $($cf.Name):$($i + 1) uses a natural-language path placeholder (业务插件目录)."
        }
    }
}

# 7) 工作包所有权：重复根目录必须显式豁免（设计内共享/父子结构）
$knownSharedRoots = @{
    'plugins/optimization' = @('WP-20', 'WP-21')   # module-design/optimization.md 为 WP-20/21 共用
    'ui/workflow'          = @('WP-10', 'WP-22')   # WP-22 挂靠 WP-10 ui 层（D5 裁决）
    'ui/comparison'        = @('WP-10', 'WP-22')
}
$ownershipRoots = @{}
foreach ($wf in (Get-ChildItem -LiteralPath $workPackagePath -Filter 'WP-*.md' -File)) {
    if ($wf.Name -match '^(WP-\d{2})') {
        $wpId2 = $Matches[1]
        $wRaw = Get-Content -LiteralPath $wf.FullName -Raw -Encoding UTF8
        foreach ($ownLine in ([regex]::Matches($wRaw, '(?m)^.*拥有目录.*$'))) {
            foreach ($tok in [regex]::Matches($ownLine.Value, '`[^`]*industrialrobot/([^`/]+(?:/[^`/]+)*)/?`')) {
                $rel = $tok.Groups[1].Value.TrimEnd('/')
                if (-not $ownershipRoots.ContainsKey($rel)) { $ownershipRoots[$rel] = @() }
                if ($ownershipRoots[$rel] -notcontains $wpId2) {
                    $ownershipRoots[$rel] = @($ownershipRoots[$rel]) + $wpId2
                }
            }
        }
    }
}
foreach ($rel in $ownershipRoots.Keys) {
    $owners = @($ownershipRoots[$rel])
    if ($owners.Count -gt 1) {
        $exempt = $false
        foreach ($exKey in $knownSharedRoots.Keys) {
            if (($rel -eq $exKey -or $rel -like ($exKey + '/*')) -and @($knownSharedRoots[$exKey] | Where-Object { $owners -notcontains $_ }).Count -eq 0) {
                $exempt = $true
            }
        }
        if (-not $exempt) {
            Add-ValidationError "Ownership root '$rel' is claimed by multiple WPs ($($owners -join ', ')) without an explicit shared-root exemption."
        }
    }
}

# 8) Schema 负例最低覆盖：每个 Schema 至少 3 个非法示例
$invalidDir = Join-Path $PSScriptRoot 'schemas\examples\invalid'
$invalidPerSchema2 = @{}
if (Test-Path -LiteralPath $invalidDir) {
    foreach ($ef in (Get-ChildItem -LiteralPath $invalidDir -Filter '*.example.json' -File)) {
        $stem = ($ef.BaseName -replace '\.example\.json$', '') -split '\.' | Select-Object -First 1
        if ($invalidPerSchema2.ContainsKey($stem)) { $invalidPerSchema2[$stem] = $invalidPerSchema2[$stem] + 1 }
        else { $invalidPerSchema2[$stem] = 1 }
    }
}
foreach ($schemaFile in (Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'schemas') -Filter '*.schema.json' -File)) {
    $stem = $schemaFile.BaseName -replace '\.schema$', ''
    $cnt = 0
    if ($invalidPerSchema2.ContainsKey($stem)) { $cnt = $invalidPerSchema2[$stem] }
    if ($cnt -lt 3) {
        Add-ValidationError "Schema '$stem' has $cnt invalid example(s); at least 3 required under schemas/examples/invalid/."
    }
}

# 9) 任务状态账本：覆盖率、状态取值、Ready⇒前置全 Done＋签署、Done⇒签署＋SHA＋证据
$ledgerPath = Join-Path $agentTaskPath 'task-status.md'
if (-not (Test-Path -LiteralPath $ledgerPath)) {
    Add-ValidationError 'agent-tasks/task-status.md is missing.'
} else {
    $ledgerStates = @{}
    foreach ($ll in (Get-Content -LiteralPath $ledgerPath -Encoding UTF8)) {
        if ($ll -match '^\|\s*(WP-\d{2}-T\d{2})\s*\|\s*(Planned|Ready|Blocked|Done)\s*\|\s*([^|]*)\|\s*([^|]*)\|\s*([^|]*)\|\s*([^|]*)\|') {
            $ledgerStates[$Matches[1]] = @{ State = $Matches[2]; Signer = $Matches[4].Trim(); Note = $Matches[6].Trim() }
        }
    }
    foreach ($t in @($cardTaskIds)) {
        if (-not $ledgerStates.ContainsKey($t)) {
            Add-ValidationError "Task $t has no row in agent-tasks/task-status.md."
        }
    }
    foreach ($t in $ledgerStates.Keys) {
        if (-not $cardTaskIds.Contains($t)) {
            Add-ValidationError "task-status.md contains row $t without a matching task card."
            continue
        }
        $st = $ledgerStates[$t]
        if ($st.State -eq 'Ready') {
            foreach ($p in $taskPrereqs[$t]) {
                if (-not $ledgerStates.ContainsKey($p) -or $ledgerStates[$p].State -ne 'Done') {
                    Add-ValidationError "Task $t is Ready but prerequisite $p is not Done."
                }
            }
            if ([string]::IsNullOrWhiteSpace($st.Signer) -or $st.Signer -eq '-') {
                Add-ValidationError "Task $t is Ready without a signer in task-status.md."
            }
        }
        if ($st.State -eq 'Done') {
            if ([string]::IsNullOrWhiteSpace($st.Signer) -or $st.Signer -eq '-') {
                Add-ValidationError "Task $t is Done without a signer in task-status.md."
            }
            if ($st.Note -notmatch '[0-9a-f]{7,40}') {
                Add-ValidationError "Task $t is Done without a commit SHA in its note."
            }
            if ($st.Note -notmatch 'out/test-evidence') {
                Add-ValidationError "Task $t is Done without an out/test-evidence path in its note."
            }
        }
    }
}

# 10) 任务卡不得携带落后于当前基线的版本/计数（以派生为准）
foreach ($cf in $cardFilesForDag) {
    $raw2 = Get-Content -LiteralPath $cf.FullName -Raw -Encoding UTF8
    if ($raw2 -match 'v0\.7|124 项|124 行|124 requirements|124 trace|Generated 124|-eq 124|P0=110') {
        Add-ValidationError "Task card $($cf.Name) references a stale baseline version/count; derive from current requirements.md instead."
    }
}

# 11) DOCUMENT-BASELINE 头部必须与当前状态一致
$baselineHead = (Get-Content -LiteralPath $baselinePath -TotalCount 20) -join "`n"
foreach ($probe in @('IRD-D10-20260829', 'v0.8', 'v1.3')) {
    if (-not $baselineHead.Contains($probe)) {
        Add-ValidationError "DOCUMENT-BASELINE.md header is missing current-state token '$probe'."
    }
}

# 12) 状态行矛盾：状态行为 Accepted 时不得同时包含待签署/待评审
foreach ($dd in @($architecturePath, $moduleDesignPath)) {
    foreach ($sf in (Get-ChildItem -LiteralPath $dd -Filter '*.md' -Recurse -File)) {
        $head = Get-Content -LiteralPath $sf.FullName -TotalCount 10 -Encoding UTF8
        foreach ($hl in $head) {
            if ($hl -match '(文档状态|治理状态|目录状态|> 状态)') {
                if ($hl -match 'Accepted' -and $hl -match '待签署|待评审') {
                    Add-ValidationError "$($sf.Name) has a contradictory status line (Accepted + 待签署/待评审)."
                }
            }
        }
    }
}

# 13) public-interfaces §7 值对象全部拥有符号注册
$piText = Get-Content -LiteralPath (Join-Path $architecturePath 'public-interfaces.md') -Raw -Encoding UTF8
$symbolNames = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
foreach ($line in ($symbolRegistryText -split "`r?`n")) {
    if ($line -match '^\|\s*`SYM-[^`]+`\s*\|\s*`([^`]+)`') {
        [void]$symbolNames.Add($Matches[1])
    }
}
$inSection7 = $false
foreach ($pline in ($piText -split "`r?`n")) {
    if ($pline -match '^## 7\.') { $inSection7 = $true; continue }
    if ($inSection7 -and $pline -match '^## ') { break }
    if ($inSection7 -and $pline -match '^\|\s*`([A-Za-z0-9_]+)`') {
        $voName = $Matches[1]
        if (-not $symbolNames.Contains($voName)) {
            Add-ValidationError "public-interfaces §7 value object '$voName' is not registered in symbol-registry.md."
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
Write-Output "$requirementTotal requirements, 19 acceptance tests, $contractCount contracts, $symbolCount symbols, $adrCount ADRs, 0 trace gaps"
