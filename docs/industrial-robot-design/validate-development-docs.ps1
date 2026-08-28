$ErrorActionPreference = 'Stop'

$requirementsPath = Join-Path $PSScriptRoot '..\industrial-robot-design-software-requirements.md'
$masterPlanPath = Join-Path $PSScriptRoot '..\industrial-robot-design-development-task-breakdown.md'
$tracePath = Join-Path $PSScriptRoot 'requirement-traceability.csv'
$generatorPath = Join-Path $PSScriptRoot 'generate-traceability.ps1'
$workPackagePath = Join-Path $PSScriptRoot 'work-packages'

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
            $pipeCount = ($line.ToCharArray() | Where-Object { $_ -eq '|' }).Count
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

foreach ($requiredPath in @($requirementsPath, $masterPlanPath, $tracePath, $generatorPath, $workPackagePath)) {
    if (-not (Test-Path -LiteralPath $requiredPath)) {
        Add-ValidationError "Missing required path: $requiredPath"
    }
}

if ($errors.Count -eq 0) {
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
        'implementation_task', 'test_task', 'review_task', 'acceptance_scenario', 'phase', 'status'
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
    foreach ($number in 0..12) {
        $prefix = 'WP-{0:D2}-' -f $number
        if (@($workPackageFiles | Where-Object { $_.Name.StartsWith($prefix) }).Count -ne 1) {
            Add-ValidationError "Expected exactly one detailed work-package plan beginning with $prefix."
        }
    }

    $documents = @($requirementsPath, $masterPlanPath) + @($workPackageFiles.FullName)
    foreach ($document in $documents) {
        Test-MarkdownTables -Path $document
        $documentText = Get-Content -LiteralPath $document -Raw -Encoding UTF8
        if ($documentText -match '(?i)TODO|TBD|PLACEHOLDER|\[To be written\]|待补充|待定') {
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
        if (-not [System.Linq.Enumerable]::SequenceEqual[byte]($expectedBytes, $actualBytes)) {
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

Write-Output '124 requirements, 19 acceptance tests, 0 trace gaps'
