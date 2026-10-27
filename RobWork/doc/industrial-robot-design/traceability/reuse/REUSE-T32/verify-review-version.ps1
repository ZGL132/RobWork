# REUSE-T32：按 reuse-contract.ps1 SUITABILITY 同口径验证 review.version 树
# 内上游文件与当前工作树逐字节一致。
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$RepoRoot = 'D:/10_Source_Repos/21_robot/RobWork'
$version = 'c65ffc4fc495da0b01b2060ff3049eb127091d72'
function Get-Digest([string]$text) {
  $bytes = [Text.Encoding]::UTF8.GetBytes(($text.TrimStart([char]0xFEFF) -replace "`r`n", "`n").TrimEnd("`n") + "`n")
  $sha = [Security.Cryptography.SHA256]::Create()
  try { return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant() } finally { $sha.Dispose() }
}
$paths = @('RobWork/RobWork/src/rw/math/MetricFactory.hpp',
           'RobWork/RobWork/src/rw/pathplanning/PathAnalyzer.hpp',
           'RobWork/RobWork/src/rw/math/CMakeLists.txt',
           'RobWork/RobWork/src/rw/pathplanning/CMakeLists.txt',
           'RobWork/RobWork/src/rwlibs/pathplanners/CMakeLists.txt',
           'RobWork/RobWork/src/rwlibs/pathplanners/rrt/RRTPlanner.hpp',
           'RobWork/RobWork/src/rw/pathplanning/QConstraint.hpp',
           'RobWork/RobWork/src/rw/pathplanning/QSampler.hpp',
           'RobWork/RobWork/src/rw/pathplanning/QEdgeConstraint.hpp',
           'RobWork/RobWork/src/rw/pathplanning/StopCriteria.hpp')
$fail = 0
foreach ($path in $paths) {
  $saved = $ErrorActionPreference
  try { $ErrorActionPreference = 'Continue'; $raw = & git -C $RepoRoot show "${version}:$path" 2>$null; $rc = $LASTEXITCODE } finally { $ErrorActionPreference = $saved }
  if ($rc) { Write-Output "MISSING $path"; $fail++; continue }
  $old = Get-Digest ($raw -join "`n")
  $now = Get-Digest ([IO.File]::ReadAllText((Join-Path $RepoRoot $path)))
  if ($old -eq $now) { Write-Output "MATCH $path" } else { Write-Output "DIFF $path old=$old now=$now"; $fail++ }
}
if ($fail) { exit 1 }
Write-Output 'ALL-MATCH'
