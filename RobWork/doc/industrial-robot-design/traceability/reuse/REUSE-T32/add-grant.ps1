# REUSE-T32：向 permissions.json 登记 trajectory/src/Planner.cpp 的精确 grant
# （本包改动使其进入 functionalChanges 全量 AD 检查；类别 model+solver、
# 零对象操作——WorkCell/Device 只经 runtime 只读视图借持，无构造/写面）。
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$ErrorActionPreference = 'Stop'
$RepoRoot = 'D:/10_Source_Repos/21_robot/RobWork'
function Get-Digest([string]$text) {
  $bytes = [Text.Encoding]::UTF8.GetBytes(($text.TrimStart([char]0xFEFF) -replace "`r`n", "`n").TrimEnd("`n") + "`n")
  $sha = [Security.Cryptography.SHA256]::Create()
  try { return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant() } finally { $sha.Dispose() } }
function FB([string]$rel) {
  return [pscustomobject]@{ path = $rel; sha256 = (Get-Digest ([IO.File]::ReadAllText((Join-Path $RepoRoot $rel)))) }
}
$pp = 'RobWork/doc/industrial-robot-design/reuse/permissions.json'
$full = Join-Path $RepoRoot $pp
$p = Get-Content $full -Raw -Encoding UTF8 | ConvertFrom-Json
if ($p.grants | Where-Object { $_.id -eq 'REUSE-T32-Planner.cpp' }) { throw 'grant already present' }
$grant = [pscustomobject]@{
  id = 'REUSE-T32-Planner.cpp'
  source = (FB 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/trajectory/src/Planner.cpp')
  targets = @('sdurws_ird_trajectory')
  categories = @('model', 'solver')
  operations = @('query', 'borrow')
  origin = 'runtime-borrow'
  lifetime = 'REUSE-T32 规划器适配上游消费：WorkCell/Device/State 只经 runtime WorkCellConstView 只读借持（§10.1——findDevice 查询与 bounds 读取，零模型构造/零 State 写/零发布面）；solver 类＝rw/pathplanning（QConstraint/QSampler/QEdgeConstraint/PlannerConstraint/PathAnalyzer）与 rw/trajectory（QPath 值面）及 rwlibs/pathplanners RRTPlanner 的规划器装配消费；policy 会话经④端口直穿，不触 proximity 直链。'
  permissionRef = 'ARCH:3.6.1'
  usageRole = 'adapter'
  build = (FB 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/trajectory/CMakeLists.txt')
}
$p.grants = @($p.grants) + @($grant)
[IO.File]::WriteAllText($full, ($p | ConvertTo-Json -Depth 40), [Text.UTF8Encoding]::new($false))
Write-Output 'grant REUSE-T32-Planner.cpp added'
