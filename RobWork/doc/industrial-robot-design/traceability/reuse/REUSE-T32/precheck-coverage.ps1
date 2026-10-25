# REUSE-T32 覆盖面自检（与 reuse-contract.ps1 COVERAGE 同口径）：
# 改动产品文件的每个非空行须落在某行 sources 区段内，且覆盖该文件的行
# 全部 decision∉(pending,proposal)／status∈(migrated,closed)。
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$ErrorActionPreference = 'Stop'
$RepoRoot = 'D:/10_Source_Repos/21_robot/RobWork'
$matrix = Get-Content (Join-Path $RepoRoot 'RobWork/doc/industrial-robot-design/reuse/functions/trajectory.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$files = @('RobWork/RobWorkStudio/src/rwslibs/industrialrobot/trajectory/src/Planner.cpp',
           'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/trajectory/CMakeLists.txt',
           'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/trajectory/test/PlannerReuseTest.cpp')
$fail = 0
foreach ($f in $files) {
  $covered = @{}
  $rowsForFile = @()
  foreach ($r in $matrix.functions) {
    foreach ($s in $r.sources) {
      if ($s.path -eq $f) {
        $rowsForFile += $r
        foreach ($n in ([int]$s.startLine)..([int]$s.endLine)) { $covered[$n] = $true }
      }
    }
  }
  $txt = ([IO.File]::ReadAllText((Join-Path $RepoRoot $f))).TrimStart([char]0xFEFF)
  $lines = @(($txt -replace "`r`n", "`n") -split "`n")
  $n = 0; $missing = @()
  foreach ($line in $lines) { $n++; if ($line.Trim() -and -not $covered.ContainsKey($n)) { $missing += $n } }
  $unresolved = @($rowsForFile | Where-Object { $_.decision -in @('pending','proposal') -or $_.status -notin @('migrated','closed') })
  Write-Output ("$f : lines=" + $lines.Count + " uncovered=" + $missing.Count + $(if ($missing.Count -le 20) { ' at: ' + ($missing -join ',') } else { ' first: ' + (($missing | Select-Object -First 20) -join ',') }) + " coveringRows=" + $rowsForFile.Count + " unresolved=" + $unresolved.Count)
  if ($unresolved.Count) { foreach ($u in $unresolved) { Write-Output ("  UNRESOLVED: " + $u.functionId + " " + $u.status + "/" + $u.decision) } }
  if ($missing.Count -or $unresolved.Count) { $fail++ }
}
if ($fail) { exit 1 }
Write-Output 'COVERAGE-PRECHECK-ALL-GREEN'
