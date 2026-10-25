# REUSE-T36 独立验收辅助（12-catalog-matrix-check）：COLLISION capability、
# random/LHS 矩阵行、findings F-961、migration §13 摘录的机读核对。
[Console]::OutputEncoding = [Text.Encoding]::UTF8
$design = 'RobWork/doc/industrial-robot-design'

Write-Output '=== catalog COLLISION ==='
$c = Get-Content "$design/reuse/catalog.json" -Raw -Encoding UTF8 | ConvertFrom-Json
$cap = $c.capabilities | Where-Object { $_.id -eq 'COLLISION' }
Write-Output ("props: " + (($cap.PSObject.Properties.Name) -join ', '))
Write-Output ("status: " + $cap.status)
Write-Output ("baseline/version fields: " + ($cap.PSObject.Properties | Where-Object { $_.Name -match 'version|baseline' } | ForEach-Object { $_.Name + '=' + $_.Value }))
$up = $cap.upstream
if (-not $up) { $up = $cap.upstreamFiles }
Write-Output ("upstream entries: " + @($up).Count)
foreach ($u in @($up)) {
    Write-Output (" - " + $u.path + " | target=" + $u.target + " | sha=" + $u.sha256)
}

Write-Output ''
Write-Output '=== kinematics/optimization random-LHS rows ==='
foreach ($unit in @('kinematics', 'optimization')) {
    $m = Get-Content "$design/reuse/functions/$unit.json" -Raw -Encoding UTF8 | ConvertFrom-Json
    $rows = $m.functions | Where-Object { $_.functionId -match 'Random|random|Sample|sampler|Sampler|LHS|lhs|Optimi' } | Select-Object -First 12
    Write-Output ("[$unit] matched rows:")
    foreach ($r in $rows) {
        Write-Output (" - " + $r.functionId + " | " + $r.decision + "/" + $r.status)
        if ($r.search) { Write-Output ("   search: " + ($r.search.command -replace '\s+', ' ')) }
        if ($r.nonOverlapEvidence) { Write-Output ("   nonOverlap: " + $r.nonOverlapEvidence) }
    }
}

Write-Output ''
Write-Output '=== findings.json F-961 ==='
$f = Get-Content "$design/traceability/findings.json" -Raw -Encoding UTF8 | ConvertFrom-Json
$items = $f.findings
if (-not $items) { $items = $f.items }
foreach ($x in @($items)) {
    if ($x.id -match 'F-961' -or $x.findingId -match 'F-961') {
        $x | ConvertTo-Json -Depth 5
    }
}
