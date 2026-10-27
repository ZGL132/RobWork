# REUSE-T32 catalog 编辑：MATH-METRIC/PATH-PLANNING 升 verified（evidence 绑定
# + version 树核验在案）；TRAJ-LINEAR search.conclusion 增补候选2定案结论。
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$ErrorActionPreference = 'Stop'
$RepoRoot = 'D:/10_Source_Repos/21_robot/RobWork'
$catalogPath = Join-Path $RepoRoot 'RobWork/doc/industrial-robot-design/reuse/catalog.json'
function Get-Digest([string]$text) {
  $bytes = [Text.Encoding]::UTF8.GetBytes(($text.TrimStart([char]0xFEFF) -replace "`r`n", "`n").TrimEnd("`n") + "`n")
  $sha = [Security.Cryptography.SHA256]::Create()
  try { return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant() } finally { $sha.Dispose() }
}
$catalog = Get-Content $catalogPath -Raw -Encoding UTF8 | ConvertFrom-Json
$version = 'c65ffc4fc495da0b01b2060ff3049eb127091d72'
foreach ($spec in @(
    @{ id = 'MATH-METRIC';    ev = 'RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T32/math-metric-suitability.md' },
    @{ id = 'PATH-PLANNING';  ev = 'RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T32/path-planning-suitability.md' })) {
  $cap = $catalog.capabilities | Where-Object { $_.id -eq $spec.id }
  if (-not $cap) { throw "capability missing: $($spec.id)" }
  $cap.review.status = 'verified'
  $cap.review.version = $version
  $evPath = Join-Path $RepoRoot $spec.ev
  $cap.review | Add-Member -NotePropertyName evidence -NotePropertyValue ([pscustomobject]@{
    path = $spec.ev
    sha256 = (Get-Digest ([IO.File]::ReadAllText($evPath)))
  }) -Force
  Write-Output "$($spec.id) -> verified; evidence digest $($cap.review.evidence.sha256)"
}
$tl = $catalog.capabilities | Where-Object { $_.id -eq 'TRAJ-LINEAR' }
if (-not $tl) { throw 'TRAJ-LINEAR missing' }
$tl.search.conclusion = '已读公共接口与全部现成求值入口；候选2定案（REUSE-T32，2026-10-25）：rw/trajectory 无稳定凸组合式求值入口（LinearInterpolator/RampInterpolator 差分式、Rotation3D 特化为 slerp、Polynomial Horner 同构差分），适配等价性结论见 GAP-TRJ-LINEAR-01；候选3待所有者裁决'
$json = $catalog | ConvertTo-Json -Depth 40
[IO.File]::WriteAllText($catalogPath, $json, [Text.UTF8Encoding]::new($false))
Write-Output 'catalog saved'
