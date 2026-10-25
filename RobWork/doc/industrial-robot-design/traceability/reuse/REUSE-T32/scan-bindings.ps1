# REUSE-T32：扫描 trajectory.json 与 production-*.json 全部绑定，与文件实际摘要比对。
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$ErrorActionPreference = 'Stop'
$RepoRoot = 'D:/10_Source_Repos/21_robot/RobWork'
function Get-Digest([string]$text) {
  $bytes = [Text.Encoding]::UTF8.GetBytes(($text.TrimStart([char]0xFEFF) -replace "`r`n", "`n").TrimEnd("`n") + "`n")
  $sha = [Security.Cryptography.SHA256]::Create()
  try { return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant() } finally { $sha.Dispose() }
}
function Check-Bind([object]$b, [string]$where) {
  $full = Join-Path $RepoRoot $b.path
  if (-not (Test-Path $full)) { Write-Output "MISSING $where -> $($b.path)"; return }
  $actual = Get-Digest ([IO.File]::ReadAllText($full))
  if ($actual -ne $b.sha256) { Write-Output "STALE $where -> $($b.path) bound=$($b.sha256.Substring(0,12)) actual=$($actual.Substring(0,12))" }
}
$matrix = Get-Content (Join-Path $RepoRoot 'RobWork/doc/industrial-robot-design/reuse/functions/trajectory.json') -Raw -Encoding UTF8 | ConvertFrom-Json
foreach ($r in $matrix.functions) {
  foreach ($s in $r.sources) {
    $full = Join-Path $RepoRoot $s.path
    $t = ([IO.File]::ReadAllText($full)).TrimStart([char]0xFEFF)
    $lines = @(($t -replace "`r`n", "`n") -split "`n")
    $region = ($lines[([int]$s.startLine - 1)..([int]$s.endLine - 1)] -join "`n")
    $d = Get-Digest $region
    if ($d -ne $s.sha256) { Write-Output "STALE-REGION $($r.functionId) -> $($s.path) $($s.startLine)-$($s.endLine) ($($s.symbol))" }
    if (-not $region.Contains("$($s.symbol)")) { Write-Output "SYMBOL-MISSING $($r.functionId) -> $($s.path) $($s.startLine)-$($s.endLine) ($($s.symbol))" }
  }
  foreach ($i in $r.inputs) { Check-Bind $i "input:$($r.functionId)" }
  if ($r.nonOverlapEvidence) { Check-Bind $r.nonOverlapEvidence "noe:$($r.functionId)" }
  if ($r.auditEvidence) { foreach ($a in $r.auditEvidence) { Check-Bind $a "aue:$($r.functionId)" } }
  if ($r.evidence) {
    Check-Bind $r.evidence "ev:$($r.functionId)"
    $rep = Get-Content (Join-Path $RepoRoot $r.evidence.path) -Raw -Encoding UTF8 | ConvertFrom-Json
    foreach ($b in $rep.bindings) { Check-Bind $b "repbind:$($r.functionId)" }
    foreach ($kind in @('normal','disconnected','oracle','upstreamComparison')) {
      $run = $rep.$kind
      Check-Bind $run.xml "xml:$($r.functionId):$kind"
      Check-Bind $run.log "log:$($r.functionId):$kind"
      if ($run.mutatedSource) { Check-Bind $run.mutatedSource "mut:$($r.functionId)" }
    }
  }
}
Write-Output 'scan done'
