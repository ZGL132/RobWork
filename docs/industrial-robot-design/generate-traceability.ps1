param(
    [string]$RequirementsPath = (Join-Path $PSScriptRoot 'requirements.md'),
    [string]$OutputPath = (Join-Path $PSScriptRoot 'requirement-traceability.csv')
)

$ErrorActionPreference = 'Stop'

function Expand-RequirementCell {
    param([string]$Cell)

    $ids = [System.Collections.Generic.List[string]]::new()
    $currentPrefix = $null
    $pattern = '(?:(?<prefix>[A-Z][A-Z0-9-]*-))?(?<start>\d+)(?:～(?<end>\d+))?'

    foreach ($match in [regex]::Matches($Cell, $pattern)) {
        if ($match.Groups['prefix'].Success) {
            $currentPrefix = $match.Groups['prefix'].Value
        }
        if ([string]::IsNullOrWhiteSpace($currentPrefix)) {
            continue
        }

        $start = [int]$match.Groups['start'].Value
        $end = if ($match.Groups['end'].Success) {
            [int]$match.Groups['end'].Value
        }
        else {
            $start
        }

        for ($number = $start; $number -le $end; $number++) {
            $id = '{0}{1:D2}' -f $currentPrefix, $number
            if (-not $ids.Contains($id)) {
                $ids.Add($id)
            }
        }
    }

    return $ids
}

function Get-WorkPackages {
    param([string]$RequirementId)

    switch -Regex ($RequirementId) {
        '^ARC-01$' { return @('WP-04', 'WP-03') }
        '^ARC-02$' { return @('WP-01') }
        '^ARC-0[34]$' { return @('WP-06', 'WP-03') }
        '^ARC-05$' { return @('WP-07', 'WP-03') }
        '^CON-01$' { return @('WP-05', 'WP-04') }
        '^CON-02$' { return @('WP-05') }
        '^CON-03$' { return @('WP-04', 'WP-05') }
        '^CON-04$' { return @('WP-08', 'WP-05') }
        '^CON-05$' { return @('WP-05') }
        '^CON-06$' { return @('WP-05', 'WP-06', 'WP-07') }
        '^TASK-' { return @('WP-08') }
        '^ERR-' { return @('WP-09') }
        '^EVI-' { return @('WP-05', 'WP-12') }
        '^MDL-06$|^MDL-14$' { return @('WP-13', 'WP-06') }
        '^MDL-' { return @('WP-13') }
        '^REQ-05$' { return @('WP-14', 'WP-11') }
        '^REQ-06$' { return @('WP-14', 'WP-05', 'WP-12') }
        '^REQ-' { return @('WP-14') }
        '^KIN-05$' { return @('WP-15', 'WP-07') }
        '^KIN-06$' { return @('WP-15', 'WP-10') }
        '^KIN-' { return @('WP-15') }
        '^TRJ-04$' { return @('WP-16', 'WP-07') }
        '^TRJ-' { return @('WP-16') }
        '^DYN-04$' { return @('WP-18', 'WP-17') }
        '^DYN-' { return @('WP-17') }
        '^SEL-0[12]$' { return @('WP-19', 'WP-11') }
        '^SEL-05$' { return @('WP-19', 'WP-18') }
        '^SEL-' { return @('WP-19') }
        '^OPT-0[123568]$' { return @('WP-20', 'WP-21') }
        '^OPT-' { return @('WP-21') }
        '^UX-03$' { return @('WP-09', 'WP-10', 'WP-22') }
        '^UX-08$' { return @('WP-07', 'WP-10', 'WP-22') }
        '^UX-' { return @('WP-10', 'WP-22') }
        '^NFR-COR-01$' { return @('WP-02', 'WP-23') }
        '^NFR-COR-02$' { return @('WP-23', 'WP-08') }
        '^NFR-COR-03$' { return @('WP-03', 'WP-11', 'WP-23') }
        '^NFR-COR-04$' { return @('WP-12', 'WP-05', 'WP-23') }
        '^NFR-COR-05$' { return @('WP-07', 'WP-23') }
        '^NFR-PERF-01$|^NFR-PERF-03$' { return @('WP-23', 'WP-10') }
        '^NFR-PERF-' { return @('WP-23', 'WP-08') }
        '^NFR-REL-01$' { return @('WP-04', 'WP-23') }
        '^NFR-REL-0[23]$' { return @('WP-08', 'WP-23') }
        '^NFR-REL-04$' { return @('WP-11', 'WP-04') }
        '^NFR-REL-05$' { return @('WP-09') }
        '^NFR-MNT-01$' { return @('WP-03', 'WP-01') }
        '^NFR-MNT-02$' { return @('WP-01', 'WP-24') }
        '^NFR-MNT-03$' { return @('WP-03', 'WP-09') }
        '^NFR-MNT-0[45]$' { return @('WP-01') }
        '^NFR-MNT-06$' { return @('WP-24', 'WP-01') }
        '^NFR-MNT-07$' { return @('WP-01', 'WP-06', 'WP-07') }
        '^NFR-DEP-04$' { return @('WP-04', 'WP-24') }
        '^NFR-DEP-' { return @('WP-24', 'WP-01') }
        '^NFR-SEC-0[123]$' { return @('WP-11', 'WP-24') }
        '^NFR-SEC-0[456]$' { return @('WP-24') }
        '^NFR-SEC-07$' { return @('WP-09', 'WP-24') }
        default { throw "No work-package mapping for requirement $RequirementId" }
    }
}

if (-not (Test-Path -LiteralPath $RequirementsPath)) {
    throw "Requirements document not found: $RequirementsPath"
}

$text = Get-Content -LiteralPath $RequirementsPath -Raw -Encoding UTF8
$requirementMatches = [regex]::Matches(
    $text,
    '(?m)^\|\s*([A-Z][A-Z0-9-]*-\d+)\s*\|\s*(P[01])\s*\|\s*(.*?)\s*\|\s*$'
)

$traceStart = $text.IndexOf('## 16. 需求—验收追踪')
$traceEnd = $text.IndexOf('## 17.', $traceStart)
if ($traceStart -lt 0 -or $traceEnd -lt 0) {
    throw 'Could not locate the requirement traceability section.'
}

$traceText = $text.Substring($traceStart, $traceEnd - $traceStart)
$acceptanceById = @{}

foreach ($line in ($traceText -split "`r?`n")) {
    if ($line -notmatch '^\|') {
        continue
    }

    $cells = @($line.Trim('|').Split('|') | ForEach-Object { $_.Trim() })
    if ($cells.Count -ne 4 -or $cells[0] -eq '需求 ID' -or $cells[0] -match '^---') {
        continue
    }

    foreach ($id in (Expand-RequirementCell -Cell $cells[0])) {
        $entry = [pscustomobject]@{
            Method = $cells[1]
            Scenario = $cells[2]
            Phase = $cells[3]
        }

        if (-not $acceptanceById.ContainsKey($id)) {
            $acceptanceById[$id] = [System.Collections.Generic.List[object]]::new()
        }
        $acceptanceById[$id].Add($entry)
    }
}

$rows = foreach ($match in $requirementMatches) {
    $id = $match.Groups[1].Value
    $priority = $match.Groups[2].Value
    $summary = $match.Groups[3].Value.Trim()
    $packages = @(Get-WorkPackages -RequirementId $id)
    $acceptanceEntries = @($acceptanceById[$id])

    if ($acceptanceEntries.Count -eq 0) {
        throw "Requirement $id has no acceptance trace entry."
    }

    $methods = @($acceptanceEntries | ForEach-Object { $_.Method } | Select-Object -Unique)
    $scenarios = @($acceptanceEntries | ForEach-Object { $_.Scenario } | Select-Object -Unique)
    $phases = @($acceptanceEntries | ForEach-Object { $_.Phase } | Select-Object -Unique)

    [pscustomobject][ordered]@{
        requirement_id = $id
        priority = $priority
        requirement_summary = $summary
        work_package = $packages -join ';'
        implementation_task = @($packages | ForEach-Object { "$_-IMP-$id" }) -join ';'
        test_task = @($packages | ForEach-Object { "$_-TEST-$id" }) -join ';'
        review_task = @($packages | ForEach-Object { "$_-REV-$id" }) -join ';'
        acceptance_scenario = (($methods -join ' / ') + '；' + ($scenarios -join ' / '))
        phase = $phases -join '/'
        status = 'Planned'
    }
}

$duplicateIds = @($rows | Group-Object requirement_id | Where-Object Count -ne 1)
if ($duplicateIds.Count -gt 0) {
    throw "Duplicate requirement rows: $($duplicateIds.Name -join ', ')"
}
if ($rows.Count -ne 124) {
    throw "Expected 124 requirement rows, found $($rows.Count)."
}

$rows | Export-Csv -LiteralPath $OutputPath -NoTypeInformation -Encoding utf8BOM
Write-Output "Generated $($rows.Count) traceability rows at $OutputPath"
