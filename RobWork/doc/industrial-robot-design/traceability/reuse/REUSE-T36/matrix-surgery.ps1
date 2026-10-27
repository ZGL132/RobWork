# REUSE-T36 矩阵手术：policy.json 全文件解决批（T32 matrix-surgery.ps1 同款机制）。
# ①CE 14 行重钉（新行号）＋处置（2 adapter＋12 business-specific……实际 1 adapter
#   ＋13 business-specific，RCE ctor 为第 2 个 adapter）
# ②RCE 4 行处置（1 adapter＋3 business-specific，区段不变——文件零改动）
# ③新增 2 helper 行（escapeRegexLiteral/toUpstreamRulePattern）＋CE 6 audit 行
#   ＋测试文件 14 audit 行铺面
# ④全部 CE inputs 摘要刷新＋契约审计绑定刷新
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$ErrorActionPreference = 'Stop'
$RepoRoot = 'D:/10_Source_Repos/21_robot/RobWork'
$CE  = 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/policy/src/CollisionEvaluator.cpp'
$RCE = 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/policy/src/RobWorkCollisionEvaluator.cpp'
$TS  = 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/policy/test/CollisionSessionTest.cpp'
$CM  = 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/policy/CMakeLists.txt'
$EV  = 'RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T36'
$matrixPath = Join-Path $RepoRoot 'RobWork/doc/industrial-robot-design/reuse/functions/policy.json'

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
$bCE = FileBind $CE; $bRCE = FileBind $RCE; $bTS = FileBind $TS; $bCM = FileBind $CM
$bREADME = FileBind "$EV/README.md"

$matrix = Get-Content $matrixPath -Raw -Encoding UTF8 | ConvertFrom-Json
$rows = $matrix.functions
function Get-Row([string]$id) {
  $r = $rows | Where-Object { $_.functionId -eq $id }
  if (-not $r) { throw "row missing: $id" }
  return $r
}

# ---- CE 区段重钉表（应用 F-961 diff 后 853 行）----
$ceSpan = @{
  'policy.CollisionEvaluator.sceneObjectRoleToken'                    = @(54, 74)
  'policy.CollisionEvaluator.trySceneObjectRole'                      = @(75, 88)
  'policy.CollisionEvaluator.detail.canonicalPair'                    = @(103, 117)
  'policy.CollisionEvaluator.detail.classifyDomainPair'               = @(118, 181)
  'policy.CollisionEvaluator.detail.computeSessionIdentity'           = @(182, 229)
  'policy.CollisionEvaluator.detail.makeBuiltinBackendDescriptor'     = @(232, 257)
  'policy.CollisionEvaluator.ResolvedCollisionScope.inScopePairCount.all'   = @(258, 274)
  'policy.CollisionEvaluator.ResolvedCollisionScope.excludedPairCount'      = @(275, 286)
  'policy.CollisionEvaluator.ResolvedCollisionScope.inScopePairCount.domain' = @(287, 299)
  'policy.CollisionEvaluator.(anonymous namespace).validateSceneAndPolicy'  = @(314, 452)
  'policy.CollisionEvaluator.(anonymous namespace).expandRuleTarget'        = @(453, 491)
  'policy.CollisionEvaluator.(anonymous namespace).expandRulesAndCheckConflicts' = @(492, 561)
  'policy.CollisionEvaluator.CollisionEvaluationSession.CollisionEvaluationSession' = @(640, 801)
  'policy.CollisionEvaluator.CollisionEvaluationSession.buildPolicyProximitySetup'  = @(802, 851)
}
foreach ($id in $ceSpan.Keys) {
  $r = Get-Row $id
  $r.sources[0] = RegionBind $CE $r.sources[0].symbol $ceSpan[$id][0] $ceSpan[$id][1]
}

# ---- ①处置：adapter 行（真实上游消费）＋business-specific 行 ----
# bps：ProximitySetup 系直接消费（setUseExcludeStaticPairs/setUseIncludeAll/
# addProximitySetupRule/makeExclude/makeInclude）；F-961 单点适配在案。
$r = Get-Row 'policy.CollisionEvaluator.CollisionEvaluationSession.buildPolicyProximitySetup'
$r.decision = 'adapter'; $r.status = 'migrated'
$r.differences = '真实上游setup关闭静态默认过滤并include-all，再按名称经toUpstreamRulePattern唯一适配点转规则（F-961修复：REGEX前缀完整名改传REGEX+字面转义，上游regex_match全匹配原名；普通名透传零扰动）；makeExclude不是字面API（REUSE-T36实核）；setup公开输出，evaluate逐scope对直接query，不据此声称碰撞实际误判'
$r | Add-Member -NotePropertyName production -NotePropertyValue ([pscustomobject]@{
  entry = 'CollisionEvaluationSession::buildPolicyProximitySetup'; upstream = 'addProximitySetupRule';
  consumer = 'CollisionEvaluationSession::buildPolicyProximitySetup'; target = 'sdurws_ird_policy' }) -Force
$r | Add-Member -NotePropertyName evidence -NotePropertyValue (FileBind "$EV/production-buildPolicyProximitySetup.json") -Force

# RCE ctor：内置 ProximityStrategyRW 后端实例构造（COLLISION 能力构造消费点）。
$r = Get-Row 'policy.RobWorkCollisionEvaluator.RobWorkCollisionEvaluator.RobWorkCollisionEvaluator'
$r.decision = 'adapter'; $r.status = 'migrated'
$r.differences = '内置后端实例化：构造rw::proximity::ProximityStrategyRW（仅CollisionStrategy能力——距离能力归运行期dynamic_cast探测）＋评估器级查询互斥＋复现要素冻结值注入（detail::makeBuiltinBackendDescriptor单点）；构造不做几何注册（模型注册归会话构建期POL-T07）；REUSE-T36变异证明断开后端实例即全会话构建fail-fast'
$r | Add-Member -NotePropertyName production -NotePropertyValue ([pscustomobject]@{
  entry = 'RobWorkCollisionEvaluator::RobWorkCollisionEvaluator'; upstream = 'ProximityStrategyRW';
  consumer = 'RobWorkCollisionEvaluator::RobWorkCollisionEvaluator'; target = 'sdurws_ird_policy' }) -Force
$r | Add-Member -NotePropertyName evidence -NotePropertyValue (FileBind "$EV/production-RobWorkCollisionEvaluator.json") -Force

# 其余 16 行（CE 12＋RCE 3……CE 14 行中 bps 为 adapter，RCE 4 行中 ctor 为 adapter）
$businessIds = @(
  'policy.CollisionEvaluator.sceneObjectRoleToken',
  'policy.CollisionEvaluator.trySceneObjectRole',
  'policy.CollisionEvaluator.detail.canonicalPair',
  'policy.CollisionEvaluator.detail.classifyDomainPair',
  'policy.CollisionEvaluator.detail.computeSessionIdentity',
  'policy.CollisionEvaluator.detail.makeBuiltinBackendDescriptor',
  'policy.CollisionEvaluator.ResolvedCollisionScope.inScopePairCount.all',
  'policy.CollisionEvaluator.ResolvedCollisionScope.excludedPairCount',
  'policy.CollisionEvaluator.ResolvedCollisionScope.inScopePairCount.domain',
  'policy.CollisionEvaluator.(anonymous namespace).validateSceneAndPolicy',
  'policy.CollisionEvaluator.(anonymous namespace).expandRuleTarget',
  'policy.CollisionEvaluator.(anonymous namespace).expandRulesAndCheckConflicts',
  'policy.CollisionEvaluator.CollisionEvaluationSession.CollisionEvaluationSession',
  'policy.RobWorkCollisionEvaluator.createSession',
  'policy.RobWorkCollisionEvaluator.RobWorkCollisionEvaluator.backend',
  'policy.RobWorkCollisionEvaluator.makeRobWorkCollisionEvaluator'
)
foreach ($id in $businessIds) {
  $r = Get-Row $id
  if ($r.decision -eq 'adapter') { throw "unexpected adapter row: $id" }
  $r.decision = 'business-specific'; $r.status = 'migrated'
  $r | Add-Member -NotePropertyName nonOverlapEvidence -NotePropertyValue $bREADME -Force
}

# ---- ③inputs 摘要刷新（CE 旧摘要→新摘要；其余绑定不变）----
$ceOld = 'd7fd7e7fc0f85ab536542be0d880ceeb83e538995d487b2876b7f502a13766b2'
foreach ($r in $rows) {
  foreach ($in in $r.inputs) {
    if ($in.path -eq $CE -and $in.sha256 -eq $ceOld) { $in.sha256 = $bCE.sha256 }
  }
}

# ---- ③新增行：2 helper＋CE audit×6＋测试文件 audit×14 ----
$searchT36 = [pscustomobject]@{
  command = 'REUSE-T36 F-961落地：REUSE-T01 policy-boundary-audit §F-961＋B18独验反例＋上游ProximitySetupRule/convertToRegEx实读（candidate/目录红转绿oracle）；audit/helper区段为结构性或产品自持面'
  conclusion = '上游无字面名API（makeExclude/makeInclude全经convertToRegEx）实核；适配单点toUpstreamRulePattern镜像上游REGEX分支；audit区段为结构性/产品契约面，非上游算法核'
}
function New-AuditRow([string]$id, [string]$req, [string[]]$targets, [string]$role, [object]$src, [object[]]$inputs, [string]$diff, [string]$srcPath) {
  return [pscustomobject]@{
    functionId = $id; ownerUnit = 'policy'; requirements = @($req)
    sources = @($src); targets = $targets; usageRole = $role; permissionRef = 'ARCH:3.6'
    capabilityId = 'NONE'; decision = 'business-specific'; status = 'migrated'
    search = $searchT36; differences = $diff; inputs = $inputs
    nonOverlapEvidence = $bREADME
    auditBoundary = '逐区段实审（REUSE-T36落地批）；结构性/产品契约面登记，非上游算法消费；不授予源码修改权'
  }
}
$newRows = @()
# helper 两行（产品自持字面化转写——为上游 REGEX 约定适配而建，本身不调用上游）
$newRows += New-AuditRow 'policy.CollisionEvaluator.escapeRegexLiteral' 'NFR-MNT-03' @('sdurws_ird_policy') 'adapter' (RegionBind $CE 'escapeRegexLiteral' 562 609) @($bCM, $bCE) 'ECMAScript特殊字符全集字面化转写（产品自持，覆盖并补齐上游patternToRegEx未覆盖集合——REGEX分支绕过patternToRegEx故转义必须自足）；纯串变换无失败路径' $CE
$newRows += New-AuditRow 'policy.CollisionEvaluator.toUpstreamRulePattern' 'ARC-04' @('sdurws_ird_policy') 'adapter' (RegionBind $CE 'toUpstreamRulePattern' 610 632) @($bCM, $bCE) 'F-961唯一适配点：分支条件逐字镜像上游convertToRegEx REGEX判定（size>5且前5字节REGEX），命中传REGEX+字面转义原名，否则透传零扰动；恰5字节REGEX走上游字面分支（边界用例钉住）；镜像不调用上游，上游调整约定时须同批复核' $CE
# CE audit 空洞×6
$newRows += New-AuditRow 'policy.audit-t36.CollisionEvaluator.header' 'NFR-COR-01' @('sdurws_ird_policy') 'adapter' (RegionBind $CE 'CollisionEvaluator.cpp' 1 53) @($bCM, $bCE) '文件头设计依据（§6.4三步/CR-04/R-4纪律）与包含面（本批新增<string>——F-961适配返回串）' $CE
$newRows += New-AuditRow 'policy.audit-t36.CollisionEvaluator.identity-form' 'NFR-COR-02' @('sdurws_ird_policy') 'adapter' (RegionBind $CE 'kSessionIdentityForm' 89 102) @($bCM, $bCE) 'detail注释＋会话身份形态代/魔数常量（身份组成升代纪律）' $CE
$newRows += New-AuditRow 'policy.audit-t36.CollisionEvaluator.detail-close' 'NFR-COR-01' @('sdurws_ird_policy') 'adapter' (RegionBind $CE 'namespace detail' 230 231) @($bCM, $bCE) 'detail命名空间收尾' $CE
$newRows += New-AuditRow 'policy.audit-t36.CollisionEvaluator.anon-head' 'NFR-COR-01' @('sdurws_ird_policy') 'adapter' (RegionBind $CE 'SceneObjectIndex' 300 313) @($bCM, $bCE) '会话三步注释＋匿名空间头与对象索引/对键类型别名' $CE
$newRows += New-AuditRow 'policy.audit-t36.CollisionEvaluator.anon-close' 'NFR-COR-01' @('sdurws_ird_policy') 'adapter' (RegionBind $CE '会话构造' 633 639) @($bCM, $bCE) '匿名空间收尾＋会话构造编排注释' $CE
$newRows += New-AuditRow 'policy.audit-t36.CollisionEvaluator.tail' 'NFR-COR-01' @('sdurws_ird_policy') 'adapter' (RegionBind $CE 'namespace sdurws::ird::policy' 852 853) @($bCM, $bCE) '文件尾命名空间收尾' $CE
# 测试文件×14（全文件铺面）
$tsTargets = @('sdurws_ird_policy_test')
$tsInputs = @($bCM, $bTS)
$newRows += New-AuditRow 'policy.audit-t36.CollisionSessionTest.head' 'NFR-COR-01' $tsTargets 'owner' (RegionBind $TS 'taggedContentIdentity' 1 96) $tsInputs '测试文件头（设计依据/零Qt红线/集成条件）＋对象/内容身份打标替身' $TS
$newRows += New-AuditRow 'policy.audit-t36.CollisionSessionTest.contexts' 'POL-SCOPE-1' $tsTargets 'owner' (RegionBind $TS 'TestNameContext' 97 153) $tsInputs '名称上下文/校验上下文测试替身（IPolicyNameContext/IPolicyValidationContext 实现面）' $TS
$newRows += New-AuditRow 'policy.audit-t36.CollisionSessionTest.fakedevice' 'POL-SCOPE-1' $tsTargets 'owner' (RegionBind $TS 'FakeDevice' 154 228) $tsInputs 'FakeDevice测试替身（rw::models::Device最小实现——§7.5设备解析链夹具）' $TS
$newRows += New-AuditRow 'policy.audit-t36.CollisionSessionTest.sceneids' 'POL-SCOPE-1' $tsTargets 'owner' (RegionBind $TS 'SceneIds' 229 251) $tsInputs '标准场景对象身份集（字节打标——跨用例稳定定位）' $TS
$newRows += New-AuditRow 'policy.audit-t36.CollisionSessionTest.fixture' 'POL-SCOPE-1' $tsTargets 'owner' (RegionBind $TS 'makeScene' 252 311) $tsInputs 'addObjectFrames/makeScene场景装配（WorkCell构造＋对象帧——test-fixture例外授权面；测试脚手架装配任务输入非模型所有权主张）' $TS
$newRows += New-AuditRow 'policy.audit-t36.CollisionSessionTest.builders' 'POL-SCOPE-1' $tsTargets 'owner' (RegionBind $TS 'makeNames' 312 384) $tsInputs 'makeNames/makeContext/baseInput/publishPolicy标准装配（七段解析管线唯一发布路径）' $TS
$newRows += New-AuditRow 'policy.audit-t36.CollisionSessionTest.helpers' 'POL-SCOPE-1' $tsTargets 'owner' (RegionBind $TS 'buildSession' 385 410) $tsInputs 'buildSession便捷入口（唯一实现入口makeRobWorkCollisionEvaluator——§6.5）与findPair断言辅助' $TS
$newRows += New-AuditRow 'policy.audit-t36.CollisionSessionTest.scope1' 'POL-SCOPE-1' $tsTargets 'owner' (RegionBind $TS 'SelfDomainOnlyExpandsLinkPairsOnly_POL_SCOPE_1' 411 596) $tsInputs 'POL-SCOPE-1作用域矩阵三用例（Self域/三域逐行/Scene域必检）' $TS
$newRows += New-AuditRow 'policy.audit-t36.CollisionSessionTest.scope2' 'POL-SCOPE-2' $tsTargets 'owner' (RegionBind $TS 'ExcludedByRuleKeepsTraceAndSetupExclude_POL_SCOPE_2' 597 789) $tsInputs 'POL-SCOPE-2守卫四用例（排除留痕+setup排除/相邻默认过滤/解析期冲突/场景展开冲突同码复核）' $TS
$newRows += New-AuditRow 'policy.audit-t36.CollisionSessionTest.guards' 'ARC-04' $tsTargets 'owner' (RegionBind $TS 'RuleReferenceMissingFromSceneRejected' 790 989) $tsInputs '场景守卫八用例（规则引用/身份重复/场景身份/空workcell/主链不可解析/Group保守拒绝/空后端/域禁用）' $TS
$newRows += New-AuditRow 'policy.audit-t36.CollisionSessionTest.identity' 'NFR-COR-02' $tsTargets 'owner' (RegionBind $TS 'SameInputsYieldEqualSessionIdentity' 990 1067) $tsInputs '会话身份三用例（同输入等价/组成敏感性/承载序无关——§6.4确定性）' $TS
$newRows += New-AuditRow 'policy.audit-t36.CollisionSessionTest.detector' 'ARC-05' $tsTargets 'owner' (RegionBind $TS 'UniqueEntryProducesBuiltinBackend' 1068 1146) $tsInputs '唯一实现入口/词表环回/F-808必检不隐藏（矩阵未列组合保守拒绝）' $TS
$newRows += New-AuditRow 'policy.audit-t36.CollisionSessionTest.f961-banner' 'NFR-MNT-03' $tsTargets 'owner' (RegionBind $TS 'F-961' 1147 1170) $tsInputs 'F-961名规则用例段注释（上游REGEX约定与字面适配说明）' $TS
$newRows += New-AuditRow 'policy.audit-t36.CollisionSessionTest.f961-cases' 'ARC-04' $tsTargets 'owner' (RegionBind $TS 'RegexPrefixedNamesExcludeRuleMatchesLiteral_F961' 1171 1285) $tsInputs 'F-961三用例（REGEX前缀排除规则match原名+存储pattern字面断言/普通点号名透传零扰动/恰5字节REGEX上游字面分支边界钉）' $TS

$matrix.functions = @($rows) + @($newRows)
[IO.File]::WriteAllText($matrixPath, ($matrix | ConvertTo-Json -Depth 40), [Text.UTF8Encoding]::new($false))
Write-Output ("matrix saved: total rows " + $matrix.functions.Count)
Write-Output ("CE=" + $bCE.sha256)
Write-Output ("RCE=" + $bRCE.sha256)
Write-Output ("TS=" + $bTS.sha256)
