# WP-00-T03 fixture runner: execute the documentation gate against minimal fixture
# trees and assert, per fixture, that the gate exits non-zero, prints the expected
# diagnostic keyword (WP-00 plan §6), and never touches the official document tree.
#
# Fixture model: the official docs/industrial-robot-design tree is copied to a
# temporary directory at run time; each fixture injects exactly one violation into
# the copy; a copy of validate-development-docs.ps1 is placed in the copy and run
# there. No full document copies are stored in this repository.

param()

$ErrorActionPreference = 'Stop'

$fixturesRoot = $PSScriptRoot
$designRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$gateScript = Join-Path $designRoot 'validate-development-docs.ps1'

function Get-TreeStateHash {
    param([string]$Root)
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        $combined = New-Object System.Text.StringBuilder
        $files = @(Get-ChildItem -LiteralPath $Root -Recurse -File | Sort-Object FullName)
        foreach ($file in $files) {
            $relative = $file.FullName.Substring($Root.Length).TrimStart('\')
            $hash = $sha.ComputeHash([System.IO.File]::ReadAllBytes($file.FullName))
            [void]$combined.Append($relative + ':' + [BitConverter]::ToString($hash) + ';')
        }
        $final = $sha.ComputeHash([System.Text.Encoding]::UTF8.GetBytes($combined.ToString()))
        return [BitConverter]::ToString($final)
    }
    finally {
        $sha.Dispose()
    }
}

function Copy-DesignTree {
    param([string]$Destination)
    New-Item -ItemType Directory -Path $Destination -Force | Out-Null
    Copy-Item -Path (Join-Path $designRoot '*') -Destination $Destination -Recurse -Force
    # 夹具目录本身不进入被测树（门禁不扫描它，但排除可保持被测树即正式树）。
    $nestedFixtures = Join-Path $Destination 'fixtures'
    if (Test-Path -LiteralPath $nestedFixtures) { Remove-Item -LiteralPath $nestedFixtures -Recurse -Force }
}

function Invoke-GateInTree {
    param([string]$TreeRoot)
    $gateCopy = Join-Path $TreeRoot 'validate-development-docs.ps1'
    Copy-Item -LiteralPath $gateScript -Destination $gateCopy -Force
    # 被测门禁/生成器的诊断走 stderr；2>&1 合并后属于 ErrorRecord，
    # 会与本执行器的 $ErrorActionPreference='Stop' 相互作用而中断，故局部降级。
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $output = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $gateCopy 2>&1
    }
    finally {
        $ErrorActionPreference = $previousPreference
    }
    $text = @($output | ForEach-Object { $_.ToString() }) -join "`n"
    return @{ ExitCode = $LASTEXITCODE; Output = $text }
}

$officialBefore = Get-TreeStateHash -Root $designRoot

# Clean baseline: the unmodified tree must pass the gate (exit 0).
$cleanTree = Join-Path ([System.IO.Path]::GetTempPath()) ('wp00-clean-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
Copy-DesignTree -Destination $cleanTree
$cleanRun = Invoke-GateInTree -TreeRoot $cleanTree
Remove-Item -LiteralPath $cleanTree -Recurse -Force
$cleanOk = ($cleanRun.ExitCode -eq 0)
Write-Output ("clean-tree exit=" + $cleanRun.ExitCode + " pass=" + $cleanOk)
if (-not $cleanOk) {
    Write-Output ($cleanRun.Output | Select-Object -First 1)
}

# Keyword alternates per WP-00 plan §6; the first keyword found in gate output counts.
$fixtures = @(
    @{ Name = 'missing-requirement';   Keywords = @('数量错误', 'CSV 缺失') }
    @{ Name = 'duplicate-requirement'; Keywords = @('IDs not unique') }
    @{ Name = 'empty-acceptance';      Keywords = @('no acceptance trace') }
    @{ Name = 'missing-work-package';  Keywords = @('unique detailed plan') }
    @{ Name = 'stale-csv';             Keywords = @('CSV stale') }
    @{ Name = 'invalid-release';       Keywords = @('invalid release') }
    @{ Name = 'orphan-task';           Keywords = @('Task ID without card') }
    @{ Name = 'table-shape';           Keywords = @('table separators') }
)

$results = foreach ($fixture in $fixtures) {
    $temp = Join-Path ([System.IO.Path]::GetTempPath()) ('wp00-fixture-' + $fixture.Name + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
    Copy-DesignTree -Destination $temp
    $inject = Join-Path $fixturesRoot (Join-Path $fixture.Name 'inject.ps1')
    # 子进程执行注入脚本：隔离变量作用域，并让 $LASTEXITCODE 真实反映注入结果
    # （进程内 & 调用不会重置 $LASTEXITCODE，会被上一夹具的门禁退出码污染）。
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        [void](& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $inject -TempRoot $temp 2>&1)
    }
    finally {
        $ErrorActionPreference = $previousPreference
    }
    if ($LASTEXITCODE -ne 0) { throw ("fixture injection failed: " + $fixture.Name) }
    $run = Invoke-GateInTree -TreeRoot $temp
    Remove-Item -LiteralPath $temp -Recurse -Force

    $nonZero = ($run.ExitCode -ne 0)
    $matchedKeyword = $null
    $keywordLine = ''
    foreach ($keyword in $fixture.Keywords) {
        $line = @($run.Output -split "`n" | Where-Object { $_.Contains($keyword) } | Select-Object -First 1)
        if ($line.Count -gt 0) { $matchedKeyword = $keyword; $keywordLine = $line[0].Trim(); break }
    }
    [pscustomobject]@{
        Fixture  = $fixture.Name
        ExitCode = $run.ExitCode
        NonZero  = $nonZero
        Keyword  = $(if ($matchedKeyword) { $matchedKeyword } else { '<MISSING>' })
        Pass     = ($nonZero -and $matchedKeyword)
        Evidence = $keywordLine
    }
}

$officialAfter = Get-TreeStateHash -Root $designRoot
$officialUnchanged = ($officialBefore -eq $officialAfter)

$results | Format-Table -AutoSize | Out-String -Width 260 | Write-Output
foreach ($result in $results) {
    Write-Output ($result.Fixture + ' | exit=' + $result.ExitCode + ' | keyword=' + $result.Keyword)
    if ($result.Evidence) { Write-Output ('  diag: ' + $result.Evidence) }
}
Write-Output ("clean-tree-pass=" + $cleanOk + " official-tree-hash-unchanged=" + $officialUnchanged)
Write-Output ("official-tree-hash-before=" + $officialBefore)
Write-Output ("official-tree-hash-after=" + $officialAfter)

$failed = @($results | Where-Object { -not $_.Pass })
if ($failed.Count -eq 0 -and $cleanOk -and $officialUnchanged) {
    Write-Output 'ALL FIXTURES PASSED (non-zero exit + keyword + official tree unchanged)'
    exit 0
}
Write-Output ("FIXTURES FAILED: " + (($failed | ForEach-Object { $_.Fixture }) -join ', '))
exit 1
