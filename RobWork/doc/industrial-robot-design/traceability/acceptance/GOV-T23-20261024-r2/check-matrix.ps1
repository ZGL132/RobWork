# GOV-T23 独验：只校核本包新增或变化的矩阵绑定；不重写实施证据。
param([string]$OutputDir = $PSScriptRoot)
$ErrorActionPreference = 'Stop'
$matrixPath = 'RobWork/doc/industrial-robot-design/reuse/functions/foundation-governance.json'
$beforeRaw = & git show ('4d0f1ab024f093b65d9839d090b124da721aa69f:' + $matrixPath)
if ($LASTEXITCODE) { throw 'cannot read base matrix' }
$before = ($beforeRaw -join "`n") | ConvertFrom-Json
$after = Get-Content -LiteralPath $matrixPath -Raw -Encoding utf8 | ConvertFrom-Json
function TextDigest([string]$Value) {
    $normalized = ($Value.TrimStart([char]0xFEFF).Replace("`r`n", "`n")).TrimEnd("`n") + "`n"
    $hash = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($hash.ComputeHash([Text.Encoding]::UTF8.GetBytes($normalized)))).Replace('-', '').ToLowerInvariant() }
    finally { $hash.Dispose() }
}
$checks = @()
$transitions = @()
$oldById = @{}
foreach ($row in $before.functions) { $oldById[$row.functionId] = $row }
foreach ($row in $after.functions) {
    $old = $oldById[$row.functionId]
    if (-not $old -or $old.status -ne $row.status -or $old.decision -ne $row.decision) {
        $transitions += [pscustomobject]@{functionId=$row.functionId; beforeStatus=$old.status; afterStatus=$row.status; beforeDecision=$old.decision; afterDecision=$row.decision}
    }
    foreach ($kind in @('sources','inputs','auditEvidence')) {
        foreach ($binding in $row.$kind) {
            $oldBindings = @($old.$kind | Where-Object { $_.path -eq $binding.path -and ($kind -ne 'sources' -or $_.symbol -eq $binding.symbol) })
            if ($oldBindings.Count -eq 1 -and ($oldBindings[0] | ConvertTo-Json -Depth 10 -Compress) -eq ($binding | ConvertTo-Json -Depth 10 -Compress)) { continue }
            $text = [IO.File]::ReadAllText((Join-Path (Get-Location) $binding.path))
            $hasSymbol = $true
            if ($kind -eq 'sources') {
                $lines = $text.TrimStart([char]0xFEFF).Replace("`r`n", "`n") -split "`n"
                if ($binding.startLine -lt 1 -or $binding.endLine -gt $lines.Count) { throw 'source region outside file' }
                $text = $lines[($binding.startLine-1)..($binding.endLine-1)] -join "`n"
                $hasSymbol = $text.Contains($binding.symbol)
            }
            $actual = TextDigest $text
            $checks += [pscustomobject]@{functionId=$row.functionId;kind=$kind;path=$binding.path;startLine=$binding.startLine;endLine=$binding.endLine;expected=$binding.sha256;actual=$actual;symbolPresent=$hasSymbol;passed=($actual -eq $binding.sha256 -and $hasSymbol)}
        }
    }
}
$bad = @($checks | Where-Object { -not $_.passed })
$falseClosure = @($transitions | Where-Object { $_.afterStatus -eq 'migrated' -and $_.beforeStatus -ne 'migrated' })
$report = [pscustomobject]@{baseCommit='4d0f1ab024f093b65d9839d090b124da721aa69f';reviewedCommit='85cfc8f570edc66fa7b20195e75e3587ab9c8e3d';passed=($checks.Count-$bad.Count);failed=$bad.Count;skipped=0;notRun=0;newMigrated=$falseClosure.Count;transitions=$transitions;checks=$checks}
$report | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $OutputDir 'matrix-check.json') -Encoding utf8
Write-Output ('changed bindings checked={0} passed={1} failed={2}; new migrated={3}' -f $checks.Count, $report.passed, $report.failed, $falseClosure.Count)
$transitions | Format-Table | Out-String | Write-Output
if ($bad.Count -or $falseClosure.Count) { exit 1 }

