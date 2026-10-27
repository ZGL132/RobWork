# REUSE-T36 production.json 生成器（ird-reuse-production/1 ×2 adapter 行；
# T32 gen-production.ps1 同款机制）。
# 绑定面＝行 sources+inputs＋capability upstream 全路径（reuse-contract
# POSITIVE/FRESHNESS 口径）；normal/oracle/upstreamComparison 引用恢复后
# 全量绿跑 final-recheck；disconnected 为各自变异红例（恢复复测＝同绿跑）。
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$ErrorActionPreference = 'Stop'
$RepoRoot = 'D:/10_Source_Repos/21_robot/RobWork'
$ev = 'RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T36'
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
$prod = 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/policy'
$ce = Bind "$prod/src/CollisionEvaluator.cpp"
$rce = Bind "$prod/src/RobWorkCollisionEvaluator.cpp"
$cmake = Bind "$prod/CMakeLists.txt"
$test = Bind "$prod/test/CollisionSessionTest.cpp"
$ceHpp = Bind "$prod/include/sdurws/ird/policy/CollisionEvaluator.hpp"
$jlHpp = Bind "$prod/include/sdurws/ird/policy/JointLimits.hpp"
$ppHpp = Bind "$prod/include/sdurws/ird/policy/PolicyParsing.hpp"
$diag = Bind "$prod/src/Diagnostics.cpp"
$errors = Bind "$prod/src/Errors.cpp"
$nameMap = Bind 'RobWork/RobWorkStudio/src/rwslibs/industrialrobot/runtime/src/NameMap.cpp'
$upProximity = @(
  'RobWork/RobWork/src/rw/proximity/CollisionDetector.hpp',
  'RobWork/RobWork/src/rw/proximity/CollisionStrategy.hpp',
  'RobWork/RobWork/src/rw/proximity/DistanceStrategy.hpp',
  'RobWork/RobWork/src/rw/proximity/rwstrategy/ProximityStrategyRW.hpp',
  'RobWork/RobWork/src/rw/proximity/ProximitySetup.hpp',
  'RobWork/RobWork/src/rw/proximity/ProximitySetupRule.hpp',
  'RobWork/RobWork/src/rw/proximity/ProximitySetupRule.cpp',
  'RobWork/RobWork/src/rw/proximity/CMakeLists.txt')
$runCmd = 'VS x64: pwsh -NoProfile -File RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T36/run-tests.ps1 -RepoRoot D:/10_Source_Repos/21_robot/RobWork -Label <label> -Filter <filter>'
$finalRun = RunEntry 'final-recheck' 0 $runCmd

function MutEntry([string]$label, [string]$sourcePath, [string]$origSha, [string]$mutFile, [string]$desc) {
  $r = RunEntry $label 1 $runCmd
  $r | Add-Member -NotePropertyName mutation -NotePropertyValue ([pscustomobject]@{
    source = $sourcePath
    originalSha256 = $origSha
    description = $desc
  })
  $r | Add-Member -NotePropertyName mutatedSource -NotePropertyValue (Bind "$ev/production/$mutFile")
  return $r
}

$bpsDesc = '真实生产变异：buildPolicyProximitySetup 的 F-961 唯一适配点断开（toUpstreamRulePattern 透传改为原名直传——恢复上游会按 REGEX 前缀解释完整名的缺陷形态）——CollisionSessionNameRules.RegexPrefixedNamesExcludeRuleMatchesLiteral_F961 真实失败（EXCLUDE 规则 match 原名 false、存储 pattern 未适配，exit 1；与 B18 独验/f961-red.log 同型反例），恢复最终源码后重建复测绿（final-recheck 214/214）。'
$ctorDesc = '真实生产变异：RobWorkCollisionEvaluator 构造函数的内置后端实例化断开（m_strategy 置空——评估器不产 ProximityStrategyRW 实例）——全部经 buildSession 构建的会话在 validateSceneAndPolicy 装配守卫处 fail-fast（空后端实例拒绝），CollisionSessionScope.SelfDomainOnlyExpandsLinkPairsOnly_POL_SCOPE_1 真实失败（exit 1），恢复最终源码后重建复测绿（final-recheck 214/214）。'

$reports = @(
  @{ file = 'production-buildPolicyProximitySetup.json'
     fn = 'policy.CollisionEvaluator.CollisionEvaluationSession.buildPolicyProximitySetup'
     target = 'sdurws_ird_policy'
     prodSrc = $ce; extraBindings = @($cmake, $test, $ceHpp, $jlHpp, $ppHpp, $diag, $errors, $nameMap)
     mut = (MutEntry 'disconnected-bps' "$prod/src/CollisionEvaluator.cpp" $ce.sha256 'CollisionEvaluator.mutated-bps.cpp' $bpsDesc)
     oracle = @('CollisionSessionGuards.ExcludedByRuleKeepsTraceAndSetupExclude_POL_SCOPE_2','CollisionSessionNameRules.RegexPrefixedNamesExcludeRuleMatchesLiteral_F961')
     upcmp = @('CollisionSessionUpstreamSemantics.RegexPrefixBoundaryExactlyFiveBytesStaysLiteral_F961','CollisionSessionNameRules.PlainDottedNamesPassThroughLiterally_F961')
     note = 'ProximitySetup 真实消费：setUseExcludeStaticPairs(false)/setUseIncludeAll(true)/addProximitySetupRule+ProximitySetupRule::makeExclude/makeInclude 经唯一适配点 toUpstreamRulePattern（F-961 修复——REGEX 前缀完整名改传 REGEX+字面转义，规则 regex_match 全匹配原名；普通名透传零扰动）。oracle 钉排除规则留痕与必检不可过滤；upstreamComparison 钉上游 REGEX 分支边界（恰 5 字节走字面分支）与普通名行为不变。'
  },
  @{ file = 'production-RobWorkCollisionEvaluator.json'
     fn = 'policy.RobWorkCollisionEvaluator.RobWorkCollisionEvaluator.RobWorkCollisionEvaluator'
     target = 'sdurws_ird_policy'
     prodSrc = $rce; extraBindings = @($cmake, $test, $ceHpp, $jlHpp, $ppHpp, $diag, $errors, $nameMap)
     mut = (MutEntry 'disconnected-ctor' "$prod/src/RobWorkCollisionEvaluator.cpp" $rce.sha256 'RobWorkCollisionEvaluator.mutated-ctor.cpp' $ctorDesc)
     oracle = @('CollisionSessionDetector.UniqueEntryProducesBuiltinBackend','CollisionSessionGuards.NullBackendStrategyRejected')
     upcmp = @('CollisionSessionIdentity.SameInputsYieldEqualSessionIdentity','CollisionSessionDetector.UniqueEntryProducesBuiltinBackend')
     note = '内置 ProximityStrategyRW 后端实例构造（仅 CollisionStrategy 能力——距离能力归运行期 dynamic_cast 探测，描述符 binary-collision+distance 不作能力证明）＋评估器级查询互斥（全会话共享同锁）＋复现要素冻结值（kFrozenBuiltinBackendVersion 注入 detail::makeBuiltinBackendDescriptor 单点）。断连红例证明后端实例化是全会话构建的承重面；identity 用例钉同输入同身份（会话经本实例 descriptor/strategy/mutex 装配）。'
  }
)

foreach ($r in $reports) {
  $bindings = @($r.prodSrc) + @($r.extraBindings) + @($upProximity | ForEach-Object { Bind $_ })
  $doc = [pscustomobject]@{
    schemaVersion = 'ird-reuse-production/1'
    functionId = $r.fn
    target = $r.target
    bindings = $bindings
    normal = $finalRun
    disconnected = $r.mut
    oracle = ([pscustomobject]@{ command = $runCmd; xml = (Bind "$ev/final-recheck.xml"); log = (Bind "$ev/final-recheck.log"); exitCode = 0; selectedCases = $r.oracle })
    upstreamComparison = ([pscustomobject]@{ command = $runCmd; xml = (Bind "$ev/final-recheck.xml"); log = (Bind "$ev/final-recheck.log"); exitCode = 0; selectedCases = $r.upcmp })
    note = $r.note
  }
  $out = Join-Path $RepoRoot ($ev + '/' + $r.file)
  [IO.File]::WriteAllText($out, ($doc | ConvertTo-Json -Depth 40), [Text.UTF8Encoding]::new($false))
  Write-Output "wrote $($r.file)"
}
