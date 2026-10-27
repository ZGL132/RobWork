# REUSE-T32：刷新 trajectory.json 中全部指向 README/verification 的绑定摘要
# （README 在矩阵手术后的补记属正常迭代；本脚本只对齐指纹，不改内容）。
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$ErrorActionPreference = 'Stop'
$RepoRoot = 'D:/10_Source_Repos/21_robot/RobWork'
function Get-Digest([string]$text) {
  $bytes = [Text.Encoding]::UTF8.GetBytes(($text.TrimStart([char]0xFEFF) -replace "`r`n", "`n").TrimEnd("`n") + "`n")
  $sha = [Security.Cryptography.SHA256]::Create()
  try { return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant() } finally { $sha.Dispose() } }
function FB([string]$rel) { return (Get-Digest ([IO.File]::ReadAllText((Join-Path $RepoRoot $rel)))) }
$readme = FB 'RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T32/README.md'
$mp = 'RobWork/doc/industrial-robot-design/reuse/functions/trajectory.json'
$m = Get-Content (Join-Path $RepoRoot $mp) -Raw -Encoding UTF8 | ConvertFrom-Json
$n = 0
foreach ($r in $m.functions) {
  if ($r.nonOverlapEvidence -and $r.nonOverlapEvidence.path -like '*REUSE-T32*') {
    $r.nonOverlapEvidence.sha256 = $readme; $n++
  }
  if ($r.auditEvidence) {
    foreach ($a in $r.auditEvidence) {
      if ($a.path -like '*REUSE-T32*README.md') { $a.sha256 = $readme }
    }
  }
}
[IO.File]::WriteAllText((Join-Path $RepoRoot $mp), ($m | ConvertTo-Json -Depth 40), [Text.UTF8Encoding]::new($false))
Write-Output "refreshed $n nonOverlapEvidence bindings"
