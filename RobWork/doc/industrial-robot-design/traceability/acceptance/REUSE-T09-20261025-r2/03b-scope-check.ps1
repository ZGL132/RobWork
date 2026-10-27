# REUSE-T09 r2 独立验收：T09 专属送验面（七提交链＋r1-fix 返工两提交）文件面机器核对
# 口径：与 r1-fix/02-scope-check.ps1 完全同款——verify-task-scope.ps1 同款
#       ConvertTo-ScopeRegex 正则（**->.*、*->[^/]*、?->[^/]、剥尾部全角括号注记、
#       忽略大小写、forbidden 优先），逐提交 first-parent diff 去重后比对当前契约
#       allowedFiles/forbiddenFiles。
# r2 增量：提交集在七提交（340dc88a4/9e1fe9c2e/aee6ac5bf/357d19e3d＋
#       8db7a7e78/a63157aec/b0997f8d8）之上追加返工段 cbeb9dedc/93ef0da15
#       （送验 head 93ef0da15 所在段，处置登记 REUSE-T09-20261025-r1-fix/README.md）。
$ErrorActionPreference = 'Stop'
try { [Console]::OutputEncoding = [System.Text.Encoding]::UTF8 } catch {}

$repoTop = (& git -C $PSScriptRoot rev-parse --show-toplevel)
if (-not $repoTop) { throw 'cannot resolve repo top' }

# T09 送验链七提交（契约 note 固化口径）＋ r1-fix 返工两提交（本送验 head 93ef0da15 段）。
$commits = @('340dc88a4','9e1fe9c2e','aee6ac5bf','357d19e3d','8db7a7e78','a63157aec','b0997f8d8','cbeb9dedc','93ef0da15')

$files = New-Object 'System.Collections.Generic.HashSet[string]'
foreach ($c in $commits) {
  $list = & git -C $repoTop -c core.quotepath=off diff --name-only "$c^1" $c
  if ($LASTEXITCODE -ne 0) { throw "git diff failed for $c" }
  foreach ($f in @($list)) { if ("$f".Trim()) { [void]$files.Add("$f".Replace('\', '/')) } }
}

$taskPath = Join-Path $repoTop 'RobWork/doc/industrial-robot-design/tasks/REUSE-T09.json'
$task = Get-Content -Raw -Encoding UTF8 $taskPath | ConvertFrom-Json

# ---------- 与 verify-task-scope.ps1 逐行同款的规则转换 ----------
function ConvertTo-ScopeRegex([string]$Raw) {
  $p = $Raw.Trim()
  $p = [regex]::Replace($p, '\s*（[^）]*）\s*$', '')
  $p = $p.Replace('\', '/')
  $p = $p -replace '^\./', ''
  $p = $p.TrimEnd('/')
  if (-not $p) { return $null }
  $e = [regex]::Escape($p)
  $e = $e.Replace('\*\*\*', '.*').Replace('\*\*', '.*').Replace('\*', '[^/]*').Replace('\?', '[^/]')
  return '^' + $e + '$'
}
$allowedRules = @(); foreach ($a in @($task.allowedFiles)) { if ("$a".Trim()) { $r = ConvertTo-ScopeRegex "$a"; if ($r) { $allowedRules += [pscustomobject]@{ Raw = $a.Trim(); Regex = $r } } } }
$forbiddenRules = @(); foreach ($f in @($task.forbiddenFiles)) { if ("$f".Trim()) { $r = ConvertTo-ScopeRegex "$f"; if ($r) { $forbiddenRules += [pscustomobject]@{ Raw = $f.Trim(); Regex = $r } } } }
function Find-ScopeHit([string]$Path, $Rules) {
  foreach ($r in $Rules) { if ([regex]::IsMatch($Path, $r.Regex, [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)) { return $r } }
  return $null
}

# ---------- 逐文件判定（forbidden 优先，同 verify-task-scope） ----------
$allowed = 0; $forbidden = 0; $outOfScope = 0
Write-Output ("t09-face-scope(r2): commits={0} files={1} allowedRules={2} forbiddenRules={3}" -f $commits.Count, $files.Count, $allowedRules.Count, $forbiddenRules.Count)
foreach ($path in ($files | Sort-Object)) {
  $fh = Find-ScopeHit $path $forbiddenRules
  if ($fh) { Write-Output "  [forbidden] $path  <= $($fh.Raw)"; $forbidden++; continue }
  $ah = Find-ScopeHit $path $allowedRules
  if ($ah) { Write-Output "  [allowed]   $path"; $allowed++ }
  else { Write-Output "  [out-of-scope] $path"; $outOfScope++ }
}
Write-Output ("t09-face-scope(r2): total={0} allowed={1} forbidden={2} out-of-scope={3}" -f $files.Count, $allowed, $forbidden, $outOfScope)
if ($outOfScope -gt 0) { exit 1 }
exit 0
