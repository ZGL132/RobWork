# REUSE-T32：一站式刷新 trajectory.json 内所有指向 REUSE-T32 证据（含外部
# 三份 production）的绑定摘要。production/verification/README 任何重写后
# 在跑门禁前执行本脚本。
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
  if ($r.evidence) {
    $fresh = FB $r.evidence.path
    if ($fresh -ne $r.evidence.sha256) { $r.evidence.sha256 = $fresh; $n++ }
  }
  if ($r.nonOverlapEvidence -and $r.nonOverlapEvidence.path -like '*REUSE-T32*README.md') {
    if ($readme -ne $r.nonOverlapEvidence.sha256) { $r.nonOverlapEvidence.sha256 = $readme; $n++ }
  }
}
[IO.File]::WriteAllText((Join-Path $RepoRoot $mp), ($m | ConvertTo-Json -Depth 40), [Text.UTF8Encoding]::new($false))
Write-Output "refreshed $n evidence bindings"
