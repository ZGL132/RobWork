# REUSE-T32 production.json 生成器（ird-reuse-production/1 ×5 行）。
# 绑定面＝行 sources+inputs＋capability upstream 全路径（reuse-contract
# POSITIVE/FRESHNESS 口径）；normal/oracle/upstreamComparison 引用同一
# final 绿跑（T26 先例），disconnected 为各自变异红例＋恢复复测。
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$ErrorActionPreference = 'Stop'
$RepoRoot = 'D:/10_Source_Repos/21_robot/RobWork'
$ev = 'RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T32'
function Get-Digest([string]$text) {
  $bytes = [Text.Encoding]::UTF8.GetBytes(($text.TrimStart([char]0xFEFF) -replace "`r`n", "`n").TrimEnd("`n") + "`n")
  $sha = [Security.Cryptography.SHA256]::Create()
  try { return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant() } finally { $sha.Dispose() }
}
function Bind([string]$rel) {
  $full = Join-Path $RepoRoot $rel
  return [pscustomobject]@{ path = $rel; sha256 = (Get-Digest ([IO.File]::ReadAllText($full))) }
}
function RunEntry([string]$label, [int]$exitCode, [string]$command) {
  return [pscustomobject]@{
    command = $command
    xml = (Bind "$ev/$label.xml")
    log = (Bind "$ev/$label.log")
    exitCode = $exitCode
  }
}
$planner = Bind 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/trajectory/src/Planner.cpp'
$cmake = Bind 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/trajectory/CMakeLists.txt'
$test = Bind 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/trajectory/test/PlannerReuseTest.cpp'
$plannerHpp = Bind 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/trajectory/include/sdurws/ird/trajectory/Planner.hpp'
$upMetric = @(
  'RobWork/RobWork/src/rw/math/MetricFactory.hpp',
  'RobWork/RobWork/src/rw/pathplanning/PathAnalyzer.hpp',
  'RobWork/RobWork/src/rw/math/CMakeLists.txt',
  'RobWork/RobWork/src/rw/pathplanning/CMakeLists.txt')
$upPath = @(
  'RobWork/RobWork/src/rwlibs/pathplanners/rrt/RRTPlanner.hpp',
  'RobWork/RobWork/src/rw/pathplanning/QConstraint.hpp',
  'RobWork/RobWork/src/rw/pathplanning/QSampler.hpp',
  'RobWork/RobWork/src/rw/pathplanning/QEdgeConstraint.hpp',
  'RobWork/RobWork/src/rw/pathplanning/StopCriteria.hpp',
  'RobWork/RobWork/src/rwlibs/pathplanners/CMakeLists.txt',
  'RobWork/RobWork/src/rw/pathplanning/CMakeLists.txt')
$runCmd = 'VS x64: pwsh -NoProfile -File RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T32/run-tests.ps1 -RepoRoot D:/10_Source_Repos/21_robot/RobWork -Label <label> -Filter <filter>'
$finalRun = RunEntry 'final' 0 $runCmd

function MutEntry([string]$label, [string]$mutFile, [string]$desc, [string]$origSha) {
  $r = RunEntry $label 1 $runCmd
  $r | Add-Member -NotePropertyName mutation -NotePropertyValue ([pscustomobject]@{
    source = 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/trajectory/src/Planner.cpp'
    originalSha256 = $origSha
    description = $desc
  })
  $r | Add-Member -NotePropertyName mutatedSource -NotePropertyValue (Bind "$ev/$mutFile")
  return $r
}
$l1Desc = '真实生产变异：pathJointLength 的上游 Manhattan 折叠断开为 Infinity（L1 键变 Linf）——六轴非对称端点的 L1 oracle 断言真实失败（exit 1），恢复最终源码后重建复测绿（disconnected-l1-recheck）。'
$l2Desc = '真实生产变异：sampleStraightPath 的上游 Euclidean 单对距离断开为恒 0（分段数跌至 1、样本仅端点）——直连筛查漏检中间碰撞，CollisionThenRework/BudgetExhausted 两项黄金断言真实失败（exit 1），恢复后复测绿（disconnected-l2-recheck）。'
$cllDesc = '真实生产变异：PolicySessionQConstraint::doInCollision 无碰撞发现也剪枝（规划期约束恒真）——CollisionThenReworkSucceeds 搜索未果断言真实失败（exit 1），恢复后复测绿（disconnected-cll-recheck）。'

$reports = @(
  @{ file = 'production-maxadjacentstep.json'; fn = 'trajectory.Planner.maxAdjacentStep';
     up = $upMetric; mut = (MutEntry 'disconnected-l1' 'Planner.mutated-l1.cpp' $l1Desc $planner.sha256);
     oracle = @('TrjPlannerAvoidance.CollisionThenReworkSucceeds_WP16T06_ACC1_ACC2');
     upcmp = @('TrjPlannerAvoidance.CollisionThenReworkSucceeds_WP16T06_ACC1_ACC2');
     note = '逐相邻对 Linf 距离（MetricFactory::makeInfinity 直接消费）；跨对 max 归约为产品选择序归约。延续性键仅经候选选择序（RRT 重规划黄金算例）间接观测——直接 oracle 需真实装置夹具，受零模型构造红线（IRD-GATE-AD）限制登记为覆盖限制（README）。' }
  @{ file = 'production-pathjointlength.json'; fn = 'trajectory.Planner.pathJointLength';
     up = $upMetric; mut = (MutEntry 'disconnected-l1' 'Planner.mutated-l1.cpp' $l1Desc $planner.sha256);
     oracle = @('TrjPlannerReuse.DirectAdoptPathLengthMatchesIndependentOracle_REUSE_T32','TrjPlannerAvoidance.DirectClearAdoptsStraightPath_WP16T06_ACC1');
     upcmp = @('TrjPlannerReuse.DirectAdoptPathLengthMatchesIndependentOracle_REUSE_T32');
     note = 'PathAnalyzer::pathLength 折叠 makeManhattan 逐相邻对距离；直连采纳 L1 与独立参考逐位对照（六轴非对称端点）；求和序变化（逐对先归约）为 catalog boundary 在案事项，50 节点实测相对差 1.9e-15（performance.log）。' }
  @{ file = 'production-samplestraightpath.json'; fn = 'trajectory.Planner.sampleStraightPath';
     up = $upMetric; mut = (MutEntry 'disconnected-l2' 'Planner.mutated-l2.cpp' $l2Desc $planner.sha256);
     oracle = @('TrjPlannerAvoidance.CollisionThenReworkSucceeds_WP16T06_ACC1_ACC2','TrjPlannerAvoidance.BudgetExhaustedYieldsMaterialNotInfeasible_WP16T06_ACC2');
     upcmp = @('TrjPlannerAvoidance.DirectClearAdoptsStraightPath_WP16T06_ACC1');
     note = '单对 L2（makeEuclidean 直接消费）决定等分采样分段数；断开红例证实样本网格的碰撞筛查敏感性（L2→0 即漏检中间碰撞）；采样计数公式级观测需计数替身夹具，受零模型构造红线限制登记为覆盖限制（README）。' }
  @{ file = 'production-search.json'; fn = 'trajectory.Planner.search';
     up = $upPath; mut = (MutEntry 'disconnected-cll' 'Planner.mutated-cll.cpp' $cllDesc $planner.sha256);
     oracle = @('TrjPlannerAvoidance.CollisionThenReworkSucceeds_WP16T06_ACC1_ACC2','TrjPlannerAvoidance.BudgetExhaustedYieldsMaterialNotInfeasible_WP16T06_ACC2');
     upcmp = @('TrjPlannerAvoidance.BudgetExhaustedYieldsMaterialNotInfeasible_WP16T06_ACC2');
     note = 'RRTPlanner::makeQToQPlanner/QConstraint::makeBounds+makeMerged/QEdgeConstraint::make/QSampler::makeConstrained 全链真实消费；同种子重放采纳段逐点相等（NFR-COR-02，黄金算例内置重放断言）。' }
  @{ file = 'production-doincollision.json'; fn = 'trajectory.Planner.doInCollision';
     up = $upPath; mut = (MutEntry 'disconnected-cll' 'Planner.mutated-cll.cpp' $cllDesc $planner.sha256);
     oracle = @('TrjPlannerAvoidance.UntrustedReviewNotTreatedAsCollision_WP16T06_KIN05','TrjPlannerAvoidance.CollisionThenReworkSucceeds_WP16T06_ACC1_ACC2');
     upcmp = @('TrjPlannerAvoidance.SessionPassthroughNoLocalCache_WP16T06_AT19');
     note = 'rw::pathplanning::QConstraint 子类每查询直穿 policy 会话（④端口）；KIN-05 不可采信保守剪枝由 UntrustedReview 用例钉住。' }
)

foreach ($r in $reports) {
  $bindings = @($planner, $cmake, $test, $plannerHpp) + @($r.up | ForEach-Object { Bind $_ })
  $doc = [pscustomobject]@{
    schemaVersion = 'ird-reuse-production/1'
    functionId = $r.fn
    target = 'sdurws_ird_trajectory'
    bindings = $bindings
    normal = $finalRun
    disconnected = $r.mut
    oracle = ([pscustomobject]@{ command = $runCmd; xml = (Bind "$ev/final.xml"); log = (Bind "$ev/final.log"); exitCode = 0; selectedCases = $r.oracle })
    upstreamComparison = ([pscustomobject]@{ command = $runCmd; xml = (Bind "$ev/final.xml"); log = (Bind "$ev/final.log"); exitCode = 0; selectedCases = $r.upcmp })
    note = $r.note
  }
  $out = Join-Path $RepoRoot ($ev + '/' + $r.file)
  [IO.File]::WriteAllText($out, ($doc | ConvertTo-Json -Depth 40), [Text.UTF8Encoding]::new($false))
  Write-Output "wrote $($r.file)"
}
