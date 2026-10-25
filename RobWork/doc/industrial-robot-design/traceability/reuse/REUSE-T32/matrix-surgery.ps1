# REUSE-T32 矩阵手术：trajectory.json
# ①16 条 Planner 行处置（5 adapter migrated＋11 business-specific migrated）
# ②ird_add_gtest／rotationAngleBetween 定案 ③全部绑定摘要刷新
# ④新增 audit 行铺面（Planner 空洞×4＋CMakeLists 全文件×15＋测试文件×13）
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$ErrorActionPreference = 'Stop'
$RepoRoot = 'D:/10_Source_Repos/21_robot/RobWork'
$P  = 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/trajectory/src/Planner.cpp'
$CM = 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/trajectory/CMakeLists.txt'
$TS = 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/trajectory/test/PlannerReuseTest.cpp'
$EV = 'RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T32'
$matrixPath = Join-Path $RepoRoot 'RobWork/doc/industrial-robot-design/reuse/functions/trajectory.json'

function Get-Digest([string]$text) {
  $bytes = [Text.Encoding]::UTF8.GetBytes(($text.TrimStart([char]0xFEFF) -replace "`r`n", "`n").TrimEnd("`n") + "`n")
  $sha = [Security.Cryptography.SHA256]::Create()
  try { return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant() } finally { $sha.Dispose() }
}
function FileBind([string]$rel) {
  return [pscustomobject]@{ path = $rel; sha256 = (Get-Digest ([IO.File]::ReadAllText((Join-Path $RepoRoot $rel)))) }
}
function RegionBind([string]$rel, [string]$symbol, [int]$start, [int]$end) {
  $t = ([IO.File]::ReadAllText((Join-Path $RepoRoot $rel))).TrimStart([char]0xFEFF)
  $lines = @(($t -replace "`r`n", "`n") -split "`n")
  if ($end -gt $lines.Count) { throw "region out of bounds $rel $end > $($lines.Count)" }
  $region = ($lines[($start - 1)..($end - 1)] -join "`n")
  if (-not $region.Contains($symbol)) { throw "symbol '$symbol' not in $rel ${start}-${end}" }
  return [pscustomobject]@{ path = $rel; symbol = $symbol; startLine = $start; endLine = $end; sha256 = (Get-Digest $region) }
}
$bP = FileBind $P; $bCM = FileBind $CM; $bTS = FileBind $TS
$bREADME = FileBind "$EV/README.md"; $bVER = FileBind "$EV/verification.json"
$bProd = @{}
foreach ($n in @('maxadjacentstep','pathjointlength','samplestraightpath','search','doincollision')) {
  $bProd[$n] = FileBind "$EV/production-$n.json"
}
$bGap = FileBind 'RobWork/doc/industrial-robot-design/reuse/extensions/GAP-TRJ-LINEAR-01.md'

$matrix = Get-Content $matrixPath -Raw -Encoding UTF8 | ConvertFrom-Json
# 幂等：先剥掉本包既有 audit-t32 行再重铺（区段/摘要随文件迭代刷新）。
$matrix.functions = @($matrix.functions | Where-Object { $_.functionId -notlike '*audit-t32*' })
$rows = $matrix.functions
function Get-Row([string]$id) {
  $r = $rows | Where-Object { $_.functionId -eq $id }
  if (-not $r) { throw "row missing: $id" }
  return $r
}
# ---- ①Planner 16 行：区段重钉＋处置 ----
$span = @{
  'trajectory.Planner.cancellationRequested'     = @(106, 111)
  'trajectory.Planner.alive'                     = @(112, 118)
  'trajectory.Planner.doSetLog'                  = @(152, 160)
  'trajectory.Planner.doInCollision'             = @(161, 195)
  'trajectory.Planner.parsePlannerSelection'     = @(206, 300)
  'trajectory.Planner.validateConfigPairAndBounds' = @(301, 345)
  'trajectory.Planner.sampleStraightPath'        = @(346, 391)
  'trajectory.Planner.reviewPathSequence'        = @(419, 463)
  'trajectory.Planner.maxAdjacentStep'           = @(464, 482)
  'trajectory.Planner.pathJointLength'           = @(483, 498)
  'trajectory.Planner.minNormalizedMargin'       = @(499, 515)
  'trajectory.Planner.candidatePreferred'        = @(516, 538)
  'trajectory.Planner.makeAvoidFailure'          = @(539, 561)
  'trajectory.Planner.search'                    = @(562, 826)
  'trajectory.Planner.makePathPlannerAdapter'    = @(827, 840)
  'trajectory.Planner.planPtpWithObstacleAvoidance' = @(841, 1091)
}
$adapter = @{
  'trajectory.Planner.maxAdjacentStep'    = @{ up = 'makeInfinity';   prod = 'maxadjacentstep' }
  'trajectory.Planner.pathJointLength'    = @{ up = 'pathLength';     prod = 'pathjointlength' }
  'trajectory.Planner.sampleStraightPath' = @{ up = 'makeEuclidean';  prod = 'samplestraightpath' }
  'trajectory.Planner.search'             = @{ up = 'makeQToQPlanner'; prod = 'search' }
  'trajectory.Planner.doInCollision'      = @{ up = 'QConstraint';    prod = 'doincollision' }
}
$diffText = @{
  'trajectory.Planner.maxAdjacentStep'    = '逐相邻对Linf改上游MetricFactory::makeInfinity直接消费（单对distInf逐位同式）；跨对max归约为产品选择序归约保留；无自写度量核心'
  'trajectory.Planner.pathJointLength'    = 'PathAnalyzer::pathLength折叠makeManhattan逐相邻对距离（单对dist1逐位同式）；逐对先归约求和序为catalog boundary在案事项（50节点实测相对差1.9e-15）；两处内联Σ|Δq|循环同日删除委托本函数'
  'trajectory.Planner.sampleStraightPath' = '单对L2改上游makeEuclidean直接消费（dist2逐位同式）；等分/端点/上限护栏与插值委托interpolateJointLinear保留；样本数公式经计数替身核验'
  'trajectory.Planner.search'             = 'RRTPlanner::makeQToQPlanner/QConstraint::makeBounds+makeMerged/QEdgeConstraint::make/QSampler::makeConstrained全链真实消费经运行核验；同种子重放采纳段逐点相等；零WorkCell构造、种子纪律不变'
  'trajectory.Planner.doInCollision'      = 'rw::pathplanning::QConstraint子类每查询直穿policy会话（④端口）；KIN-05不可采信保守剪枝经UntrustedReview用例钉住；无碰撞策略副本'
}
$businessDiff = @{
  'trajectory.Planner.cancellationRequested' = 'policy调用上下文：取消借持，alive恒真依赖调用期锚点（REUSE-T32实审定案，产品契约面非上游算法消费）'
  'trajectory.Planner.alive'                 = 'policy调用上下文恒真实现（REUSE-T32实审定案）；产品生命周期锚点契约，非上游算法消费'
  'trajectory.Planner.doSetLog'              = '上游QConstraint::doSetLog钩子空实现——诊断归policy会话与组装面（REUSE-T32实审定案）；纯适配样板，无独立数值核'
  'trajectory.Planner.parsePlannerSelection' = 'family/三键白名单、整串数值/正整数/有限范围是canonical配置边界，保留（REUSE-T32实审定案）；不用属性缺省掩盖缺键'
  'trajectory.Planner.validateConfigPairAndBounds' = '拒绝维度/非有限/端点越界（REUSE-T32实审定案）；不用PlannerUtil::clampPosition静默改值'
  'trajectory.Planner.reviewPathSequence'    = 'policy唯一路径复核，三态和finalized/Applicable硬守卫（REUSE-T32实审定案）；不直链proximity、不把间距不足当碰撞'
  'trajectory.Planner.minNormalizedMargin'   = '按评价区间逐轴归一最小裕量（REUSE-T32实审定案）；非欧氏度量语义，产品选择序键，保留'
  'trajectory.Planner.candidatePreferred'    = '四键全序保留；键1/键3委托已迁移的maxAdjacentStep/pathJointLength（上游度量供给），全序与身份不变（REUSE-T32）'
  'trajectory.Planner.makeAvoidFailure'      = '仅域payload（REUSE-T32实审定案）；cause/action中文与reasonToken归DiagCodes/diagnostics权威'
  'trajectory.Planner.makePathPlannerAdapter' = '仅构造现有薄适配实例（REUSE-T32实审定案）；不扩可插拔后端/自建场景'
  'trajectory.Planner.planPtpWithObstacleAvoidance' = '直连筛查→adapter→采纳构型→段构造；两处手写L1累加已删除并委托pathJointLength（上游Manhattan折叠）；跳过适用/搜索未果素材/取消语义保留（REUSE-T32）'
}
foreach ($id in $span.Keys) {
  $r = Get-Row $id
  $r.sources[0] = RegionBind $P $r.sources[0].symbol $span[$id][0] $span[$id][1]
  $newInputs = @($bCM, $bP)
  $oldInputs = @($r.inputs | Where-Object { $_.path -ne $CM -and $_.path -ne $P })
  $r.inputs = @($newInputs) + @($oldInputs)
  if ($adapter.ContainsKey($id)) {
    $r.decision = 'adapter'; $r.status = 'migrated'; $r.capabilityId = 'NONE' -as [string]
    $r.capabilityId = if ($id -in @('trajectory.Planner.maxAdjacentStep','trajectory.Planner.pathJointLength','trajectory.Planner.sampleStraightPath')) { 'MATH-METRIC' } else { 'PATH-PLANNING' }
    $r.differences = $diffText[$id]
    $r | Add-Member -NotePropertyName production -NotePropertyValue ([pscustomobject]@{
      entry = ($r.sources[0].symbol); upstream = $adapter[$id].up; consumer = ($r.sources[0].symbol); target = 'sdurws_ird_trajectory'
    }) -Force
    $r | Add-Member -NotePropertyName evidence -NotePropertyValue $bProd[$adapter[$id].prod] -Force
  } else {
    $r.decision = 'business-specific'; $r.status = 'migrated'; $r.capabilityId = 'NONE'
    if ($businessDiff.ContainsKey($id)) { $r.differences = $businessDiff[$id] }
    $r | Add-Member -NotePropertyName nonOverlapEvidence -NotePropertyValue $bREADME -Force
  }
}
# ---- ②ird_add_gtest 定案（CMakeLists 已改→须 migrated）----
$r = Get-Row 'trajectory.CMake.ird_add_gtest'
$r.sources[0] = RegionBind $CM 'ird_add_gtest' 539 551
$r.decision = 'business-specific'; $r.status = 'migrated'; $r.capabilityId = 'NONE'
$r.differences = '20单元同形薄宏（REUSE-T32定案保留）：上游ADD_RW_GTEST非CONFIG导出，不另建测试构建平台；本批仅新增PlannerReuseTest注册（集成条件段），宏体零改动'
$r.inputs = @($bCM)
$r | Add-Member -NotePropertyName nonOverlapEvidence -NotePropertyValue $bREADME -Force
# ---- ②rotationAngleBetween 定案（Continuity.cpp 零改动，反例保留）----
$r = Get-Row 'trajectory.rotationAngleBetween'
$r.decision = 'business-specific'; $r.status = 'audited'; $r.capabilityId = 'NONE'
$r.differences = '实测定案（REUSE-T32）：EAA/Rotation3DAngleMetric候选在acos分支边界系统性超C7角容差（轴对齐1e-6实测4.4e-11、一般轴pi-3e-6实测1.6e-10，超1e-12约40~160倍），产品2·atan2全网格1e-12内命中解析期望——保留手写核；EAA 0/pi邻域一阶补偿带内精确（pi-1e-9实测偏差0）已同时登记'
$r.inputs = @($bCM)
$r | Add-Member -NotePropertyName nonOverlapEvidence -NotePropertyValue $bREADME -Force
$r | Add-Member -NotePropertyName auditEvidence -NotePropertyValue @($bVER, $bGap) -Force
# ---- ③其余既有行：CMakeLists 输入摘要刷新 ----
$cmOld = 'e5ce8b3639195d80d05deca90dde10f78fc103ec1c551739b764c6433d110809'
foreach ($r in $rows) {
  foreach ($in in $r.inputs) {
    if ($in.path -eq $CM) { $in.sha256 = $bCM.sha256 }
  }
}
# ---- ④新增 audit 行 ----
$searchT32 = [pscustomobject]@{
  command = 'REUSE-T32 upstream-search：定向阅读rw/math/MetricFactory.hpp、MetricUtil.hpp、rw/pathplanning/PathAnalyzer.hpp、EAA.hpp/cpp、Quaternion.hpp、rw/trajectory 全部插值器求值式（GAP候选2定案）；产品面消费接线与测试面按REUSE-T32原件'
  conclusion = '度量三核/规划器全链为上游直接消费或已实证的产品契约；被否候选（EAA角差、上游测试宏）以实测反例登记；audit区段为结构性/产品契约面，非上游算法核'
}
function New-AuditRow([string]$id, [string]$req, [string[]]$targets, [string]$role, [object]$src, [object[]]$inputs, [string]$diff) {
  return [pscustomobject]@{
    functionId = $id; ownerUnit = 'trajectory'; requirements = @($req)
    sources = @($src); targets = $targets; usageRole = $role; permissionRef = 'ARCH:3.6'
    capabilityId = 'NONE'; decision = 'business-specific'; status = 'migrated'
    search = $searchT32; differences = $diff; inputs = $inputs
    nonOverlapEvidence = $bREADME
    auditBoundary = '逐区段实审（REUSE-T32）；结构性/产品契约面登记，非上游算法消费；不授予源码修改权'
  }
}
$newRows = @()
# Planner 空洞×4
$newRows += New-AuditRow 'trajectory.audit-t32.Planner.header' 'NFR-COR-01' @('sdurws_ird_trajectory') 'adapter' (RegionBind $P 'MetricFactory.hpp' 1 105) @($bCM, $bP) '文件头设计依据与rw基线包含面（本批新增rw/pathplanning/PathAnalyzer.hpp在原pathplanning面内——architecture-gates.log零命中）'
$newRows += New-AuditRow 'trajectory.audit-t32.Planner.qconstraint-doc' 'TRJ-03' @('sdurws_ird_trajectory') 'adapter' (RegionBind $P 'PolicySessionQConstraint' 119 151) @($bCM, $bP) 'QConstraint适配器类注与成员（适配语义三步/保守剪枝说明——实审定案，产品契约面）'
$newRows += New-AuditRow 'trajectory.audit-t32.Planner.qconstraint-members' 'TRJ-03' @('sdurws_ird_trajectory') 'adapter' (RegionBind $P 'm_session' 196 205) @($bCM, $bP) '适配器私有成员（会话共享只读＋取消上下文——所有权标注面）'
$newRows += New-AuditRow 'trajectory.audit-t32.Planner.review-outcome' 'TRJ-03' @('sdurws_ird_trajectory') 'adapter' (RegionBind $P 'ReviewOutcome' 392 418) @($bCM, $bP) '复核三态枚举与复核函数文档（§10.3样本约定/三态映射说明——实审定案）'
# CMakeLists 全文件×15（ird_add_gtest 宏行已有专行 539-551）
$cmTargets1 = @('sdurws_ird_trajectory'); $cmTargets2 = @('sdurws_ird_trajectory_test'); $cmTargets3 = @('sdurws_ird_trajectory_test','sdurws_ird_trajectory_contract_test')
$newRows += New-AuditRow 'trajectory.audit-t32.CMake.header' 'NFR-COR-01' $cmTargets1 'adapter' (RegionBind $CM 'ird_add_gtest' 1 101) @($bCM) '单元CMake头注：目标清单/宏自持说明（构建面登记，零算法）'
$newRows += New-AuditRow 'trajectory.audit-t32.CMake.lib' 'NFR-COR-01' $cmTargets1 'adapter' (RegionBind $CM 'add_library' 102 117) @($bCM) '产品库目标注册与非条件源清单'
$newRows += New-AuditRow 'trajectory.audit-t32.CMake.lib-cond' 'NFR-COR-01' $cmTargets1 'adapter' (RegionBind $CM 'target_sources' 118 153) @($bCM) '集成条件源注册（if(TARGET sdurw_kinematics)——Planner.cpp/Ptp.cpp等gating口径）'
$newRows += New-AuditRow 'trajectory.audit-t32.CMake.lib-opts' 'NFR-COR-01' $cmTargets1 'adapter' (RegionBind $CM 'target_compile_options' 154 211) @($bCM) 'C++17/UTF-8/编译选项'
$newRows += New-AuditRow 'trajectory.audit-t32.CMake.lib-link' 'NFR-COR-01' $cmTargets1 'adapter' (RegionBind $CM 'sdurws_ird_core' 212 231) @($bCM) '产品库链接面（七登记边PUBLIC链）'
$newRows += New-AuditRow 'trajectory.audit-t32.CMake.lib-link-cond' 'NFR-COR-01' $cmTargets1 'adapter' (RegionBind $CM 'sdurw_kinematics' 232 296) @($bCM) '集成条件链接面'
$newRows += New-AuditRow 'trajectory.audit-t32.CMake.alias' 'NFR-COR-01' $cmTargets1 'adapter' (RegionBind $CM 'RWS::ird::trajectory' 297 313) @($bCM) '别名目标与子目录豁免守卫'
$newRows += New-AuditRow 'trajectory.audit-t32.CMake.plugin' 'NFR-COR-01' $cmTargets1 'adapter' (RegionBind $CM 'sdurws_ird_trajectory_plugin' 314 396) @($bCM) '插件目标注册/选项/链接（§16.1装配门面）'
$newRows += New-AuditRow 'trajectory.audit-t32.CMake.test-target' 'NFR-COR-01' $cmTargets2 'adapter' (RegionBind $CM 'add_executable' 397 424) @($bCM) '测试目标注册与非条件测试源（TestMainReport/DiagCodes/BuildRedLine/PlanConfig/EvaluationDims）'
$newRows += New-AuditRow 'trajectory.audit-t32.CMake.test-reuse' 'NFR-COR-01' $cmTargets2 'adapter' (RegionBind $CM 'PlannerReuseTest' 425 444) @($bCM) 'REUSE-T32新增：PlannerReuseTest集成条件注册（本批唯一CMake生产性变更）'
$newRows += New-AuditRow 'trajectory.audit-t32.CMake.test-cond' 'NFR-COR-01' $cmTargets2 'adapter' (RegionBind $CM 'SmoothTest.cpp' 445 528) @($bCM) '条件测试源注册段（PtpSequence/ReproBenchmark/CartesianLine/PlannerAvoidance/Smooth/Recheck/TimeParam/FailureLocate/Playback/ProcessSpeed/ContinuousProcess）'
$newRows += New-AuditRow 'trajectory.audit-t32.CMake.macro-banner' 'NFR-COR-01' $cmTargets2 'adapter' (RegionBind $CM 'ird_add_gtest' 529 538) @($bCM) 'ird_add_gtest宏前置注（DTB §5.5/框架宏出处——business-specific定案说明）'
$newRows += New-AuditRow 'trajectory.audit-t32.CMake.report-target' 'NFR-COR-01' $cmTargets2 'adapter' (RegionBind $CM 'ird_add_gtest(sdurws_ird_trajectory_test)' 552 567) @($bCM) '宏调用＋契约测试目标前置注'
$newRows += New-AuditRow 'trajectory.audit-t32.CMake.contract' 'NFR-COR-01' $cmTargets3 'adapter' (RegionBind $CM 'sdurws_ird_trajectory_contract_test' 568 700) @($bCM) '契约测试目标注册/链接/定义（构建图契约＋插件零计算红线扫描）'
$newRows += New-AuditRow 'trajectory.audit-t32.CMake.ctest' 'NFR-COR-01' $cmTargets3 'adapter' (RegionBind $CM 'LABELS' 701 807) @($bCM) 'ctest LABELS与配置期守卫（ird_gates形态）'
# 测试文件×6（零模型构造红线内的纯数学面——真实装置观测面登记为限制）
$tsTargets = @('sdurws_ird_trajectory_test')
$newRows += New-AuditRow 'trajectory.audit-t32.PlannerReuseTest.decl' 'NFR-COR-01' $tsTargets 'owner' (RegionBind $TS 'PlannerReuseTest' 1 60) @($bCM, $bTS) '文件头（设计依据/集成条件口径/零模型构造红线与覆盖限制登记）与包含面'
$newRows += New-AuditRow 'trajectory.audit-t32.PlannerReuseTest.oracle' 'TRJ-03' $tsTargets 'owner' (RegionBind $TS 'referenceManhattan' 61 102) @($bCM, $bTS) '独立oracle两式（dist1/dist2同序独立重写——期望来源非上游自身）与身份构造替身'
$newRows += New-AuditRow 'trajectory.audit-t32.PlannerReuseTest.boundary' 'TRJ-02' $tsTargets 'owner' (RegionBind $TS 'axisAnglePose' 103 170) @($bCM, $bTS) '连续性角差段构造辅助＋解析期望（Quaternion半角精确构造——绕开EAA微角死区）'
$newRows += New-AuditRow 'trajectory.audit-t32.PlannerReuseTest.test1' 'TRJ-03' $tsTargets 'owner' (RegionBind $TS 'DirectAdoptPathLengthMatchesIndependentOracle_REUSE_T32' 171 230) @($bCM, $bTS) '直连采纳L1独立oracle（六轴非对称端点逐位对照＋L1/L2算式区分防串线）'
$newRows += New-AuditRow 'trajectory.audit-t32.PlannerReuseTest.test2' 'TRJ-02' $tsTargets 'owner' (RegionBind $TS 'RotationAngleCounterExampleKeepsAtan2_REUSE_T32' 231 318) @($bCM, $bTS) 'EAA被否反例（实测4.4e-11/1.6e-10超C7）＋产品2·atan2全网格1e-12'
$matrix.functions = @($rows) + @($newRows)
[IO.File]::WriteAllText($matrixPath, ($matrix | ConvertTo-Json -Depth 40), [Text.UTF8Encoding]::new($false))
Write-Output ("matrix saved: total rows " + $matrix.functions.Count)
