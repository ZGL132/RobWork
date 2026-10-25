# REUSE-T32 disconnected 变异驱动（apply|restore 两动作；文件安全流程——
# 最终副本落证据目录，恢复只走 WriteAllText，绝不 git checkout）。
# 用法：
#   mutation-cycle.ps1 -Action apply -Name l1
#   mutation-cycle.ps1 -Action restore -Name l1
param([Parameter(Mandatory=$true)][string]$Action,
      [Parameter(Mandatory=$true)][string]$Name,
      [string]$RepoRoot = "D:/10_Source_Repos/21_robot/RobWork")
$ErrorActionPreference='Stop'
$work = Join-Path $RepoRoot 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/trajectory/src/Planner.cpp'
$ev   = Join-Path $RepoRoot 'RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T32'
$finalBackup = Join-Path $ev 'Planner.final-restore.tmp'

# 变异表：Name → @(锚点原文, 变异文)（锚点唯一——apply 前后各断言一次）。
$mutations = @{
  'l1' = @('    const rw::math::QMetric::Ptr manhattan = rw::math::MetricFactory::makeManhattan<rw::math::Q>();',
           '    const rw::math::QMetric::Ptr manhattan = rw::math::MetricFactory::makeInfinity<rw::math::Q>(); // DISCONNECT: L1 折叠断开为 Linf')
  'l2' = @('    const double length = euclidean->distance(fromQ, toQ);',
           '    const double length = 0.0 * euclidean->distance(fromQ, toQ); // DISCONNECT: 采样 L2 断开为恒 0')
  'cll' = @('            return false;  // 无碰撞发现→扩展继续',
            '            return true;  // DISCONNECT: 无碰撞发现也剪枝')
}

if ($Action -eq 'apply') {
  $final = [IO.File]::ReadAllText($work)
  [IO.File]::WriteAllText($finalBackup, $final)
  $pair = $mutations[$Name]
  if (-not $pair) { throw "unknown mutation $Name" }
  if (-not $final.Contains($pair[0])) { throw "anchor missing for $Name" }
  $mutated = $final.Replace($pair[0], $pair[1])
  if ($mutated -eq $final) { throw "mutation produced no change for $Name" }
  [IO.File]::WriteAllText((Join-Path $ev ("Planner.mutated-" + $Name + ".cpp")), $mutated)
  [IO.File]::WriteAllText($work, $mutated)
  Write-Output "apply $Name OK (final backup $((Get-Item $finalBackup).Length) bytes)"
}
elseif ($Action -eq 'restore') {
  if (-not (Test-Path $finalBackup)) { throw 'final backup missing' }
  $final = [IO.File]::ReadAllText($finalBackup)
  [IO.File]::WriteAllText($work, $final)
  $pair = $mutations[$Name]
  $current = [IO.File]::ReadAllText($work)
  if ($current.Contains($pair[1])) { throw "restore failed: mutation marker still present" }
  Remove-Item $finalBackup
  Write-Output "restore $Name OK"
}
else { throw "unknown action $Action" }
